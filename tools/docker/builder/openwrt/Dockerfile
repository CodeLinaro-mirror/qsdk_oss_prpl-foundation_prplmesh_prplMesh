###############################################################
# SPDX-License-Identifier: BSD-2-Clause-Patent
# SPDX-FileCopyrightText: 2019-2020 the prplMesh contributors (see AUTHORS.md)
# This code is subject to the terms of the BSD+Patent license.
# See LICENSE file for more details.
###############################################################

####
## prplOS pre-requisites and "openwrt" user
####
FROM debian:bookworm-slim as prplos-prerequisites

ENV DEBIAN_FRONTEND=noninteractive

RUN \
    apt-get update && \
    apt-get install --yes --no-install-recommends \
    build-essential \
    ca-certificates \
    coccinelle \
    curl \
    file \
    gawk \
    git-core \
    gosu \
    iproute2 \
    iputils-ping \
    libbsd-dev \
    libdw-dev \
    libelf-dev \
    libncurses5-dev \
    locales \
    lzop \
    nmap \
    openssh-client \
    pkg-config \
    pwgen \
    python3 \
    python3-distlib \
    python3-jira \
    python3-jwt \
    python3-pip \
    python3-setuptools \
    python3-yaml \
    libpython3-dev \
    libssl-dev \
    rsync \
    signify-openbsd \
    subversion \
    sudo \
    swig \
    unzip \
    wget && \
    apt-get -y autoremove && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8
ENV LANG=en_US.utf8

RUN useradd -ms /bin/bash openwrt

####
## prplOS tree
####
FROM prplos-prerequisites as prplos-builder

USER openwrt

# The following args are mandatory, do not expect the build to
# succeed without specifying them on the command line.
#
# prplOS repository to use:
ARG PRPLOS_REPOSITORY
ENV PRPLOS_REPOSITORY=$PRPLOS_REPOSITORY

WORKDIR /home/openwrt

# Shallow clone prplOS
RUN printf '\033[1;35mCloning prplOS\n\033[0m' \
    && git clone --depth 1 --no-checkout "$PRPLOS_REPOSITORY" openwrt

WORKDIR /home/openwrt/openwrt

# Target system to build for (used for prplOS gen_config.py script):
# Example: TARGET_SYSTEM=qca_ipq95xx
ARG TARGET_SYSTEM
ENV TARGET_SYSTEM=$TARGET_SYSTEM

# prplOS version (commit hash) to build the toolchain and host tools from
ARG TOOLCHAIN_VERSION
ENV TOOLCHAIN_VERSION=$TOOLCHAIN_VERSION

# Build the toolchain, the host tools and the slowest host packages.
# Docker truncates file modification times to whole seconds when it
# commits a layer. Set them to the (whole second) commit time before
# building. Otherwise those packages are rebuilt in the next stage.
# gen_config.py deletes and re-clones ./feeds, so the files of every feed
# are set to the time of that feed's commit as well
RUN printf '\033[1;35mBuilding prplOS toolchain %s\n\033[0m' "$TOOLCHAIN_VERSION" \
    && git fetch --depth 1 origin "$TOOLCHAIN_VERSION" \
    && git checkout "$TOOLCHAIN_VERSION" \
    && git ls-files -z | xargs -0 touch -h -d "@$(git log -1 --format=%ct)" \
    && sed -i -E 's;git.openwrt.org/(feed|project);github.com/openwrt;' feeds.conf.default \
    && ./scripts/gen_config.py "$TARGET_SYSTEM" \
    && for feed in feeds/*/ ; do \
        [ -d "${feed}.git" ] || continue ; \
        ( cd "$feed" && git ls-files -z | xargs -0r touch -h -d "@$(git log -1 --format=%ct)" ) || exit 1 ; \
       done \
    && ./scripts/feeds install -p packages python3 protobuf \
    && make -j"$(nproc)" V=sc \
        toolchain/install \
        package/libs/gettext-full/host/compile \
        package/libs/ncurses/host/compile \
        package/libs/readline/host/compile \
        package/utils/bzip2/host/compile \
        package/system/opkg/host/compile \
        package/system/ucert/host/compile \
        package/system/fwtool/host/compile \
        package/feeds/packages/python3/host/compile \
        package/feeds/packages/protobuf/host/compile \
    && printf '\033[1;35mFinished building prplOS toolchain\n\033[0m'

# Which prplOS version (commit hash) to build:
ARG PRPLOS_VERSION
ENV PRPLOS_VERSION=$PRPLOS_VERSION

# Checkout the PRPLOS_VERSION. Restore the feeds.conf.default change made above
RUN printf '\033[1;35mChecking out prplOS %s\n\033[0m' "$PRPLOS_VERSION" \
    && git fetch --depth 1 origin "$PRPLOS_VERSION" \
    && git checkout -- feeds.conf.default \
    && git checkout "$PRPLOS_VERSION"

####
## Prebuilt prplOS and prplMesh dependencies, but not the prplMesh ipk itself
####
FROM prplos-builder as prplmesh-builder

# Configure prplOS:
# - Keep the hashes in the firmware (files/etc/prplwrt-version), to later
#   know if an upgrade is needed. The profiles used are appended to it too.
# - Profiles: the target system, 'debug' (additional packages that are
#   useful when developing), target-specific extras, and 'prpl' (feed-prpl,
#   including pWHM).
# - intel_mips, mxl_x86_osp_tb341 and qca_ipq95xx depend on iwlwav-iw,
#   which clashes with iw-full from the debug profile.
# - mxl_x86_osp_tb341(_v2) add the open source hostap introduced in the
#   MXL 9.1.15 code base.
# - Set the feed file modification times as in the toolchain stage, so
#   host packages prebuilt there from unchanged feeds are not rebuilt.
RUN printf '\033[1;35mConfiguring prplOS\n\033[0m' \
    && mkdir -p files/etc \
    && printf '%s=%s\n' \
        "PRPLOS_REPOSITORY" "$PRPLOS_REPOSITORY" \
        "PRPLOS_VERSION" "$PRPLOS_VERSION" \
        >> files/etc/prplwrt-version \
    && profiles="$TARGET_SYSTEM debug" \
    && case "$TARGET_SYSTEM" in \
        intel_mips|mxl_x86_osp_tb341|qca_ipq95xx) sed -i '/iw-full$/d' profiles/debug.yml ;; \
       esac \
    && case "$TARGET_SYSTEM" in \
        mxl_x86_osp_tb341) profiles="$profiles mxl_wlan_hostap_ng" ;; \
        mxl_x86_osp_tb341_v2) profiles="$profiles mxl_wlan_hostap_ng_wav700" ;; \
       esac \
    && profiles="$profiles prpl" \
    && git config --global url."https://git.w1.fi/hostap.git".insteadOf "https://w1.fi/hostap.git" \
    && sed -i -E 's;git.openwrt.org/(feed|project);github.com/openwrt;' feeds.conf.default \
    && ./scripts/gen_config.py $profiles \
    && for feed in feeds/*/ ; do \
        [ -d "${feed}.git" ] || continue ; \
        ( cd "$feed" && git ls-files -z | xargs -0r touch -h -d "@$(git log -1 --format=%ct)" ) || exit 1 ; \
       done \
    && for profile in $profiles ; do \
        printf '\nProfile %s:\n' "$profile" >> files/etc/prplwrt-version \
        && cat "profiles/$profile.yml" >> files/etc/prplwrt-version || exit 1 ; \
       done

# Note: build.sh uses the "Building prplOS" and "Finished building prplOS"
# markers to decide which part of the output goes to the log file.
# The prplMesh package built here (from the feed) is cleaned by
# scripts/build.sh before building prplMesh from the local source.
RUN printf '\033[1;35mBuilding prplOS\n\033[0m' \
    && make -j"$(nproc)" V=sc \
    && printf '\033[1;35m%s Finished building prplOS\n\033[0m'
