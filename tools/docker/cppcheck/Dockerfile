FROM debian:bullseye

# Use a Debian snapshot repository, because LTS support for bullseye -security has dropped
RUN echo "Acquire::Check-Valid-Until false;" | tee -a /etc/apt/apt.conf.d/10-nocheckvalid
RUN echo 'Acquire::https::snapshot.debian.org::Verify-Peer "false";' | tee -a /etc/apt/apt.conf.d/20-nocheckverify

RUN printf '%s\n%s\n%s\n' \
    "deb https://snapshot.debian.org/archive/debian/20260801T022406Z bullseye main" \
    "deb https://snapshot.debian.org/archive/debian-security/20260801T022406Z bullseye-security main" \
    "deb https://snapshot.debian.org/archive/debian/20260801T022406Z bullseye-updates main" \
    > /etc/apt/sources.list

ARG PACKAGES_FOR_BUILD="\
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

ARG CPPCHECK_VERSION=2.4

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
    apt-get install -y $PACKAGES_FOR_RUN && \
    rm -rf /var/lib/apt/lists/*
