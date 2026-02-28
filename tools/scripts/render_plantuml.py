#!/usr/bin/env python3

"""
Render PlantUML `.puml` files into PNG images through a PlantUML server.

Usage:
    python3 tools/scripts/render_plantuml.py -d <directory> [options]

Parameters:
    -d, --directory: Directory to search for `.puml` files (required).
    --depth: Search depth relative to `--directory` (default: 1, base directory only).
    -o, --output: Output directory for rendered `.png` files (default: `--directory`).
    -p, --port: Local PlantUML server port when `--server-url` is not set (default: 5018).
    --server-url: Full PlantUML server URL (for example, `http://localhost:5018`).
    --timeout: HTTP request timeout in seconds (default: 30).
    --retries: Retry count for transient request failures (default: 2).
    --retry-delay: Initial retry delay in seconds for exponential backoff (default: 0.5).
    --jobs: Number of parallel rendering jobs (default: 1).
    --flatten-output: Write all `.png` files directly under `--output` (default).
    --preserve-structure: Preserve source subdirectory structure under `--output`.
    -v, --verbose: Enable verbose logging.

Example:
    python3 tools/scripts/render_plantuml.py \
        -d documentation/images/plantuml/agent/traffic_separation \
        -o documentation/images/plantuml/agent/traffic_separation \
        --depth 1 --server-url http://localhost:5018 \
        --jobs 4 --preserve-structure -v
"""

from __future__ import annotations

import argparse
import logging
import os
import pathlib
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zlib
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"Invalid integer value: {value}") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("Value must be > 0")
    return parsed


def _non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"Invalid integer value: {value}") from error
    if parsed < 0:
        raise argparse.ArgumentTypeError("Value must be >= 0")
    return parsed


@dataclass(frozen=True)
class Config:
    input_dir: pathlib.Path
    output_dir: pathlib.Path
    max_depth: int
    server_url: str
    timeout_seconds: int
    retries: int
    retry_delay_seconds: float
    jobs: int
    flatten_output: bool


def _encode_6bit(value: int) -> str:
    if value < 10:
        return chr(48 + value)
    value -= 10
    if value < 26:
        return chr(65 + value)
    value -= 26
    if value < 26:
        return chr(97 + value)
    value -= 26
    if value == 0:
        return "-"
    if value == 1:
        return "_"
    return "?"


def _append_3bytes(b1: int, b2: int, b3: int) -> str:
    c1 = b1 >> 2
    c2 = ((b1 & 0x3) << 4) | (b2 >> 4)
    c3 = ((b2 & 0xF) << 2) | (b3 >> 6)
    c4 = b3 & 0x3F
    return "".join(
        (
            _encode_6bit(c1 & 0x3F),
            _encode_6bit(c2 & 0x3F),
            _encode_6bit(c3 & 0x3F),
            _encode_6bit(c4 & 0x3F),
        )
    )


def deflate_and_encode(text: str) -> str:
    compressor = zlib.compressobj(level=9, wbits=-15)
    compressed = compressor.compress(text.encode("utf-8")) + compressor.flush()

    encoded = []
    for index in range(0, len(compressed), 3):
        chunk = compressed[index:index + 3]
        if len(chunk) == 3:
            encoded.append(_append_3bytes(chunk[0], chunk[1], chunk[2]))
        elif len(chunk) == 2:
            encoded.append(_append_3bytes(chunk[0], chunk[1], 0))
        else:
            encoded.append(_append_3bytes(chunk[0], 0, 0))
    return "".join(encoded)


def _normalize_server_url(server_url: str) -> str:
    normalized = server_url.strip()
    if not normalized:
        raise ValueError("Empty PlantUML server URL")
    if not normalized.startswith(("http://", "https://")):
        raise ValueError("--server-url must start with http:// or https://")
    return normalized.rstrip("/")


def _iter_puml_files(base_dir: pathlib.Path, max_depth: int) -> Iterable[pathlib.Path]:
    # Existing semantics are preserved: with --depth=1 only files from base_dir
    # are included. Files in first-level subdirectories require --depth>=2.
    for root, dirs, filenames in os.walk(base_dir, topdown=True):
        dirs.sort()
        filenames.sort()

        root_path = pathlib.Path(root)
        relative = root_path.relative_to(base_dir)
        current_depth = 0 if str(relative) == "." else len(relative.parts)

        if current_depth >= max_depth:
            dirs[:] = []
            if current_depth > max_depth:
                continue

        if current_depth < max_depth:
            for filename in filenames:
                if filename.endswith(".puml"):
                    yield root_path / filename


def _target_path(
    source_path: pathlib.Path,
    input_dir: pathlib.Path,
    output_dir: pathlib.Path,
    flatten_output: bool,
) -> pathlib.Path:
    if flatten_output:
        return output_dir / (source_path.stem + ".png")
    relative_source = source_path.relative_to(input_dir)
    return output_dir / relative_source.with_suffix(".png")


def _validate_target_collisions(jobs: Sequence[Tuple[pathlib.Path, pathlib.Path]]) -> None:
    seen: Dict[pathlib.Path, pathlib.Path] = {}
    for source, target in jobs:
        if target in seen:
            previous = seen[target]
            raise ValueError(
                f"Output collision detected: both '{previous}' and '{source}' map to '{target}'. "
                "Use non-flat output or rename files."
            )
        seen[target] = source


def _fetch_png(url: str, timeout_seconds: int, retries: int, retry_delay_seconds: float) -> bytes:
    last_error: Optional[Exception] = None
    request = urllib.request.Request(url, headers={"User-Agent": "prplMesh-render-plantuml/1.0"})

    for attempt in range(retries + 1):
        try:
            with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
                content = response.read()
            if not content.startswith(PNG_SIGNATURE):
                raise ValueError("Server response is not a valid PNG payload")
            return content
        except (urllib.error.URLError, urllib.error.HTTPError, ValueError) as error:
            last_error = error
            should_retry = attempt < retries

            if isinstance(error, urllib.error.HTTPError):
                # Retry on transient HTTP statuses.
                should_retry = should_retry and (error.code == 429 or 500 <= error.code <= 599)

            if not should_retry:
                break

            sleep_seconds = retry_delay_seconds * (2 ** attempt)
            logging.warning(
                "Request failed (attempt %d/%d): %s. Retrying in %.2fs...",
                attempt + 1,
                retries + 1,
                error,
                sleep_seconds,
            )
            time.sleep(sleep_seconds)

    raise RuntimeError(f"Failed to fetch PNG from PlantUML server: {last_error}") from last_error


def _atomic_write_bytes(target_path: pathlib.Path, content: bytes) -> None:
    target_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="wb",
        delete=False,
        dir=target_path.parent,
        prefix=f"{target_path.name}.",
        suffix=".tmp",
    ) as temp_file:
        temp_file.write(content)
        temporary_path = pathlib.Path(temp_file.name)
    temporary_path.replace(target_path)


def _render_single(job: Tuple[pathlib.Path, pathlib.Path], config: Config) -> None:
    source_path, target_path = job
    with source_path.open("r", encoding="utf-8") as source_file:
        encoded = deflate_and_encode(source_file.read())

    url = f"{config.server_url}/png/{encoded}"
    content = _fetch_png(
        url=url,
        timeout_seconds=config.timeout_seconds,
        retries=config.retries,
        retry_delay_seconds=config.retry_delay_seconds,
    )
    _atomic_write_bytes(target_path, content)


def _parse_args(argv: Sequence[str]) -> Config:
    parser = argparse.ArgumentParser(
        description="Generate PNG files from PlantUML files using a PlantUML server."
    )
    parser.add_argument(
        "-d",
        "--directory",
        required=True,
        help="Directory to search for PlantUML files",
    )
    parser.add_argument(
        "--depth",
        type=_positive_int,
        default=1,
        help=(
            "Search depth relative to --directory. "
            "Semantics preserved from the existing script: depth=1 scans only the base directory."
        ),
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Optional output directory. Defaults to --directory",
    )
    connection_group = parser.add_mutually_exclusive_group()
    connection_group.add_argument(
        "-p",
        "--port",
        default="5018",
        help="Port where the local PlantUML server is listening (default: 5018)",
    )
    connection_group.add_argument(
        "--server-url",
        help="Full PlantUML server URL (for example: http://localhost:5018)",
    )
    parser.add_argument(
        "--timeout",
        type=_positive_int,
        default=30,
        help="HTTP request timeout in seconds",
    )
    parser.add_argument(
        "--retries",
        type=_non_negative_int,
        default=2,
        help="Number of retries for transient request failures",
    )
    parser.add_argument(
        "--retry-delay",
        type=float,
        default=0.5,
        help="Initial retry delay in seconds (exponential backoff)",
    )
    parser.add_argument(
        "--jobs",
        type=_positive_int,
        default=1,
        help="Number of parallel rendering jobs",
    )
    output_mode_group = parser.add_mutually_exclusive_group()
    output_mode_group.add_argument(
        "--flatten-output",
        dest="flatten_output",
        action="store_true",
        default=True,
        help="Write all PNG files directly under --output (default, legacy behavior)",
    )
    output_mode_group.add_argument(
        "--preserve-structure",
        dest="flatten_output",
        action="store_false",
        help="Preserve source subdirectory structure under --output",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose logging",
    )

    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s: %(message)s",
    )

    input_dir = pathlib.Path(args.directory).resolve()
    if not input_dir.exists():
        parser.error(f"Input directory does not exist: {input_dir}")
    if not input_dir.is_dir():
        parser.error(f"Input path is not a directory: {input_dir}")

    output_dir = pathlib.Path(args.output).resolve() if args.output else input_dir

    if args.retry_delay < 0:
        parser.error("--retry-delay must be >= 0")

    if args.server_url:
        try:
            server_url = _normalize_server_url(args.server_url)
        except ValueError as error:
            parser.error(str(error))
    else:
        try:
            server_url = _normalize_server_url(f"http://localhost:{args.port}")
        except ValueError as error:
            parser.error(str(error))

    return Config(
        input_dir=input_dir,
        output_dir=output_dir,
        max_depth=args.depth,
        server_url=server_url,
        timeout_seconds=args.timeout,
        retries=args.retries,
        retry_delay_seconds=args.retry_delay,
        jobs=args.jobs,
        flatten_output=args.flatten_output,
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    effective_argv = list(argv) if argv is not None else sys.argv[1:]
    config = _parse_args(effective_argv)
    source_files = list(_iter_puml_files(config.input_dir, config.max_depth))
    if not source_files:
        logging.info("No .puml files found under %s", config.input_dir)
        return 0

    jobs: List[Tuple[pathlib.Path, pathlib.Path]] = []
    for source_path in source_files:
        target_path = _target_path(
            source_path=source_path,
            input_dir=config.input_dir,
            output_dir=config.output_dir,
            flatten_output=config.flatten_output,
        )
        jobs.append((source_path, target_path))

    try:
        _validate_target_collisions(jobs)
    except ValueError as error:
        logging.error("%s", error)
        return 1
    logging.info(
        "Rendering %d PlantUML file(s) from %s to %s via %s",
        len(jobs),
        config.input_dir,
        config.output_dir,
        config.server_url,
    )

    failures = 0

    if config.jobs == 1:
        for source_path, target_path in jobs:
            try:
                _render_single((source_path, target_path), config)
                logging.info("Rendered: %s -> %s", source_path, target_path)
            except Exception as error:  # pylint: disable=broad-except
                failures += 1
                logging.error("Failed: %s (%s)", source_path, error)
    else:
        with ThreadPoolExecutor(max_workers=config.jobs) as executor:
            future_map = {
                executor.submit(_render_single, job, config): job
                for job in jobs
            }
            for future in as_completed(future_map):
                source_path, target_path = future_map[future]
                try:
                    future.result()
                    logging.info("Rendered: %s -> %s", source_path, target_path)
                except Exception as error:  # pylint: disable=broad-except
                    failures += 1
                    logging.error("Failed: %s (%s)", source_path, error)

    if failures:
        logging.error("Completed with %d failure(s)", failures)
        return 1

    logging.info("Completed successfully")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
