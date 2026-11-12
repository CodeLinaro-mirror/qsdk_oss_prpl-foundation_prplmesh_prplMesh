#!/bin/bash -e
# shellcheck disable=SC2016
###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2019-2020 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

scriptdir="$(cd "${0%/*}"; pwd)"
rootdir="${scriptdir%/*/*/*/*}"

# shellcheck source=functions.sh
. "${rootdir}/tools/functions.sh"

set -o pipefail

usage() {
    echo "usage: $(basename "$0") -d <target_device> [-c <cache_dir>] [-hiorstv]"
    echo "  options:"
    echo "      -h|--help - show this help menu"
    echo "      -v|--verbose - increase the script's verbosity"
    echo "      -c|--cache - directory to keep the prplOS git clone in, used to find"
    echo "                   the toolchain commit (default: <prplMesh>/build)"
    echo "      -d|--target-device the device to build for"
    echo "      --docker-target-stage docker target build stage (implies -i)"
    echo "      -i|--image - build the docker image only"
    echo "      -o|--prplos-version - the prplOS version (commit hash) to use"
    echo "      -r|--prplos-repository - the prplOS repository to use"
    echo "      -s|--shell-only - don't build prplMesh, drop into a shell instead"
    echo "      -t|--tag - the tag to use for the builder image"
    echo " -d is always required."
    echo ""
}

build_image() {
    build_dir="$1"
    mkdir -p "$build_dir"
    DOCKER_BUILDKIT=0 docker build --tag "$image_tag" \
           --build-arg PRPLOS_REPOSITORY="$PRPLOS_REPOSITORY" \
           --build-arg PRPLOS_VERSION="$PRPLOS_VERSION" \
           --build-arg TOOLCHAIN_VERSION="$TOOLCHAIN_VERSION" \
           --build-arg TARGET_SYSTEM="$TARGET_SYSTEM" \
           --target="$DOCKER_TARGET_STAGE" \
           "$scriptdir/" \
      | awk -v LOGFILE="$build_dir/openwrt-build.log" '
          BEGIN { p = 1; }
          /Finished building prplOS/ { p = 1; }
          p || /^make\[[0-3]\]|time:/ { print; }
          !p { print >> LOGFILE; }
          /Building prplOS/ { p = 0; }'
}

build_prplmesh() {
    build_dir="$1"
    container_name="prplmesh-builder-${TARGET_DEVICE}-$(uuidgen)"
    local command interactive
    if [ "$SHELL_ONLY" == true ] ; then
        command="bash"
        # to get an interactive shell, we have to use `-it`:
        interactive="-it"
    else
        command="./build_scripts/build.sh"
        # when doing non-interactive builds (e.g. in CI), `-t` can't
        # be used:
        interactive="-i"
    fi
    dbg "Container name will be $container_name"
    trap 'docker rm -f $container_name' EXIT
    docker run "$interactive" \
           --name "$container_name" \
           -e TARGET_SYSTEM \
           -e PRPLOS_VERSION \
           -e PRPLMESH_VERSION \
           -v "$scriptdir/scripts:/home/openwrt/openwrt/build_scripts/:ro" \
           -v "${rootdir}:/home/openwrt/prplMesh_source:ro" \
           "$image_tag" \
           "$command"
    mkdir -p "$build_dir"
    # Note: docker cp does not support globbing, so we need to copy the folder
    docker cp "${container_name}:/home/openwrt/openwrt/artifacts/" "$build_dir"
    mv "$build_dir/artifacts/"* "$build_dir"
    rm -r "$build_dir/artifacts/"
}

# Find the closest commit since PRPLOS_VERSION that changed the toolchain or host tools
# This commit will provide a docker layer with these tools (cached)
# The clone is kept in CACHE_DIR, which can be shared by concurrent builds
# (e.g. CI jobs on the same runner), so it is only accessed under a lock.
last_toolchain_commit() {
    local repo="$CACHE_DIR/prplos_toolchain_commit"

    mkdir -p "$CACHE_DIR" || return 1
    (
        flock 9 || exit 1
        # A clone that was interrupted (e.g. a cancelled job) leaves a
        # broken repository behind: start over.
        if [ -d "$repo" ] && ! git -C "$repo" rev-parse --is-bare-repository > /dev/null 2>&1 ; then
            dbg "Removing broken clone $repo" >&2
            rm -rf "$repo"
        fi
        if [ ! -d "$repo" ] ; then
            dbg "Cloning $PRPLOS_REPOSITORY into $repo" >&2
            git clone --quiet --bare --filter=blob:none \
                --config gc.auto=0 --config maintenance.auto=false \
                "$PRPLOS_REPOSITORY" "$repo" >&2 || exit 1
        fi
        if ! GIT_NO_LAZY_FETCH=1 git -C "$repo" cat-file -e "${PRPLOS_VERSION}^{commit}" 2>/dev/null ; then
            dbg "Fetching prplOS $PRPLOS_VERSION into $repo" >&2
            git -C "$repo" remote set-url origin "$PRPLOS_REPOSITORY"
            git -C "$repo" fetch --quiet --filter=blob:none origin "$PRPLOS_VERSION" >&2 || exit 1
        fi
        GIT_NO_LAZY_FETCH=1 git -C "$repo" log -1 --format=%H "$PRPLOS_VERSION" -- toolchain/ tools/
    ) 9> "$repo.lock"
}

main() {

    if ! command -v uuidgen > /dev/null ; then
        err "You need uuidgen to use this script. Please install it and try again."
        exit 1
    fi

    if ! OPTS=$(getopt -o 'hvc:d:io:r:st:' --long help,verbose,cache:,target-device:,docker-target-stage:,image,prplos-version:,prplos-repository:,shell,tag: -n 'parse-options' -- "$@"); then
        err "Failed parsing options." >&2
        usage
        exit 1
    fi

    eval set -- "$OPTS"

    SUPPORTED_TARGETS="freedom urx_ospv2 urx_osp haze turris-omnia"

    while true; do
        case "$1" in
            -h | --help)                      usage; exit 0; shift ;;
            -v | --verbose)                   VERBOSE=true; shift ;;
            -c | --cache)                     CACHE_DIR="$2"; shift ; shift ;;
            -d | --target-device)             TARGET_DEVICE="$2"; shift ; shift ;;
            --docker-target-stage)            DOCKER_TARGET_STAGE="$2"; IMAGE_ONLY=true; shift 2 ;;
            -i | --image)                     IMAGE_ONLY=true; shift ;;
            -o | --prplos-version)            PRPLOS_VERSION="$2"; shift; shift ;;
            -r | --prplos-repository)         PRPLOS_REPOSITORY="$2"; shift; shift ;;
            -s | --shell)                     SHELL_ONLY=true; shift ;;
            -t | --tag)                       TAG="$2"; shift ; shift ;;
            -- ) shift; break ;;
            * ) err "unsupported argument $1"; usage; exit 1 ;;
        esac
    done

    if [ "$SHELL_ONLY" == true ] && [ "$IMAGE_ONLY" == true ] ; then
        err "--shell and --image cannot be used together."
        usage
        exit 1
    fi

    case "$TARGET_DEVICE" in
        turris-omnia)
            TARGET_SYSTEM=mvebu
            ;;
        haze)
            TARGET_SYSTEM=ipq807x
            ;;
        urx_osp)
            TARGET_SYSTEM=mxl_x86_osp_tb341
            ;;
        urx_ospv2)
            TARGET_SYSTEM=mxl_x86_osp_tb341_v2
            ;;
        freedom)
            TARGET_SYSTEM=qca_ipq95xx
            ;;
        mozart)
            TARGET_SYSTEM=mtk_filogic
            ;;
        *)
            err "Unknown target device: $TARGET_DEVICE"
            info "Currently supported targets are:"
            for i in $SUPPORTED_TARGETS ; do
                info "$i"
            done
            exit 1
            ;;
    esac

    dbg "CACHE_DIR=$CACHE_DIR"
    dbg "PRPLOS_REPOSITORY=$PRPLOS_REPOSITORY"
    dbg "PRPLOS_VERSION=$PRPLOS_VERSION"
    dbg "IMAGE_ONLY=$IMAGE_ONLY"
    dbg "TAG=$TAG"
    dbg "TARGET_SYSTEM=$TARGET_SYSTEM"

    if [ -n "$TAG" ] ; then
        image_tag="$TAG"
    else
        image_tag="${DOCKER_TARGET_STAGE}-${TARGET_DEVICE}:${PRPLOS_VERSION}"
        dbg "image tag not set, using default value $image_tag"
    fi

    export PRPLOS_REPOSITORY
    export PRPLOS_VERSION
    export TARGET_SYSTEM
    
    if ! TOOLCHAIN_VERSION="$(last_toolchain_commit)" || [ -z "$TOOLCHAIN_VERSION" ] ; then
        err "Failed to find the prplOS toolchain version for $PRPLOS_VERSION."
        exit 1
    fi
    dbg "TOOLCHAIN_VERSION=$TOOLCHAIN_VERSION"
    PRPLMESH_VERSION="$(git describe --always --dirty)"
    export PRPLMESH_VERSION

    build_directory="$rootdir/build"

    build_image "$build_directory/$TARGET_DEVICE"
    [ $IMAGE_ONLY = true ] && exit $?

    build_prplmesh "$build_directory/$TARGET_DEVICE"
}

VERBOSE=false
IMAGE_ONLY=false
CACHE_DIR="${rootdir}/build"
PRPLOS_REPOSITORY='https://gitlab.com/prpl-foundation/prplos/prplos.git'
# prplos latest-24.10_2026-11-11
PRPLOS_VERSION='2028939a4ebb744dd056095592de8c946b44601e'
DOCKER_TARGET_STAGE="prplmesh-builder"
SHELL_ONLY=false

main "$@"
