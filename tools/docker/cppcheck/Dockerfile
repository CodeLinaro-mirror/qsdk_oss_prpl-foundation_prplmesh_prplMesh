FROM debian:trixie

ARG PACKAGES_FOR_BUILD="\
    ca-certificates \
    binutils \
    cmake \
    gcc \
    g++ \
    git \
    python3 \
    wget"

ARG PACKAGES_FOR_RUN="\
    bash \
    git"

ARG CPPCHECK_VERSION=2.6

RUN apt-get update && \
    apt-get install -y $PACKAGES_FOR_BUILD && \
    git clone https://github.com/danmar/cppcheck.git && \
    git -C cppcheck checkout "$CPPCHECK_VERSION" && \
    cmake -Hcppcheck -Bcppcheck -DUSE_MATCHCOMPILER=ON && \
    make -C cppcheck && \
    make -C cppcheck install && \
    rm -rf cppcheck && \
    apt-get purge -y --autoremove $PACKAGES_FOR_BUILD && \
    rm -rf /var/lib/apt/lists/*

RUN apt-get update && \
    apt-get install -y $PACKAGES_FOR_RUN