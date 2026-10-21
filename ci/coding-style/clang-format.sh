#!/bin/bash

set -e

script="$(realpath "${BASH_SOURCE[0]}")"
scriptdir="${script%/*}"
rootdir="${script%/*/*/*}"

function hostrun() {
  tag="$(sha256sum "${scriptdir}"/clang-format.dockerfile | cut -c1-8)"

  if [ -z "$(docker images -q clang-format:"${tag}")" ]; then
    DOCKER_BUILDKIT=1 docker build -f "${scriptdir}/clang-format.dockerfile" -t clang-format:"${tag}" "${scriptdir}"
  fi

  dockeropts=(
    -v "${rootdir}:${rootdir}"
    --user "$(id -u):$(id -g)"
    --workdir "${rootdir}"
    --name clang-format
    --hostname clang-format-docker
    --rm
  )

  docker run --entrypoint "${script}" "${dockeropts[@]}" "clang-format:${tag}" 
}

function dockerrun() {
  git ls-files -- '*.c' '*.cpp' '*.h' '*.hpp' '*.cc' | grep -v -E -f "${scriptdir}/.clang-ignore" | xargs clang-format -i -style=file
  exit 0
}

function main() {
  hostname="$(hostname)"
  if [[ "${hostname}" == "clang-format-docker" ]]; then
    dockerrun "$@"
  else
    hostrun "$@"
  fi
}

main "$@"

