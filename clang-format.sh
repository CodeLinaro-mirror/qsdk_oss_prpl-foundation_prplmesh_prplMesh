#! /bin/sh
scriptdir="$(cd "${0%/*}" && pwd)"
rootdir="$scriptdir"

# shellcheck source=tools/functions.sh
. "${rootdir}/tools/functions.sh"

# cd to the script directory:
cd "${0%/*}" || { echo "Couldn't cd to ${0%/*}!"; exit 1; }

IGNORE_FILE=".clang-ignore"
filter="grep ."
if test -f "$IGNORE_FILE"; then
    filter="grep -v -E -f .clang-ignore"
else
    filter="grep ."
fi

if ! command -v clang-format-6 >/dev/null 2>&1; then
    echo "clang-format-6 not found. Starting Docker..."

    docker run --rm -i \
        -v "$(pwd):/workspace" \
        -w /workspace \
        "${DOCKER_REGISTRY}prplmesh-clang-format:latest" \
        "/workspace/$(basename "$0")"

    exit $?
fi

find . -type f \( \
    -name '*.c' \
    -o -name '*.cpp' \
    -o -name '*.h' \
    -o -name '*.hpp' \
    -o -name '*.cc' \
\) -print | $filter | xargs clang-format-6 -i -style=file
