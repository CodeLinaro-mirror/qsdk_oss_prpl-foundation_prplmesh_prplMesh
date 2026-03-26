#!/bin/bash
###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2019-2020 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

scriptdir="$(cd "${0%/*}" || exit 1; pwd)"
rootdir="${scriptdir%/*/*}"

# shellcheck source=functions.sh
. "${rootdir}/tools/functions.sh"

usage() {
    echo "usage: $(basename "$0") [-hvbipt]"
    echo "  mandatory:"
    echo "      -i|--image - specify the image(s) to build (can be specified multiple times)"
    echo "                   supported values: builder/runner"
    echo "  options:"
    echo "      -h|--help - show this help menu"
    echo "      -p|--push - push each image to the registry (must be logged in)"
    echo "      -t|--tag - specify the tag(s) to add to the built images (can be specified multiple times)"
    echo "      -a|--arguments - specify build-time arguments for the image"
    echo "      -v|--verbose - verbose output"
}


main() {
    if ! OPTS=$(getopt -o 'hb:i:pt:a:v' --long help,base-image:,image:,push,tag:,argument:,verbose -n 'parse-options' -- "$@"); then
        err "Failed parsing options." >&2
        usage
        exit 1
    fi

    eval set -- "$OPTS"

    while true; do
        case "$1" in
            -h | --help)            usage; exit 0; shift ;;
            -p | --push)            push=true; shift ;;
            -t | --tag)             tags+=(":$2"); shift ; shift ;;
            -a | --argument)        build_arguments+=("$2"); shift ; shift ;;
            -i | --image)           build_images+=("$2"); shift ; shift ;;
            -v | --verbose)         export VERBOSE=true; shift ;;
            -- ) shift; break ;;
            * ) err "unsupported argument $1"; usage; exit 1 ;;
        esac
    done

    # Make sure that a build image was specified
    if [ -z "${build_images[*]}" ]; then
        err "build image not specified!"
        usage; exit 1
    fi

    dbg "TAGS=${tags[*]}"
    dbg "ARGUMENTS=${build_arguments[*]}"
    dbg "BUILD_IMAGES=${build_images[*]}"
    dbg "postfix=$postfix"
    dbg "rootdir=$rootdir"

    for image in "${build_images[@]}"; do
        image_fixed="$(printf "%s" "$image" | tr -cs 'A-Za-z0-9_' '-')"
        for tag in "${tags[@]}"; do
            full_image="${DOCKER_REGISTRY}prplmesh-$image_fixed$postfix$tag"
            info "Generating $image docker image ($full_image)"

            build_options=(--tag "$full_image" "${scriptdir}/${image}")
            for build_arg in "${build_arguments[@]}"; do
                build_options=("${build_options[@]}" --build-arg "$build_arg")
            done
            run docker image build "${build_options[@]}" || exit $?
            info "Generated $full_image"

            if [ "$push" = true ]; then
                run docker push "$full_image"
            fi
        done
    done
}

build_images=()
build_arguments=()
postfix=""
arguments_string=""
push=false

main "$@"
