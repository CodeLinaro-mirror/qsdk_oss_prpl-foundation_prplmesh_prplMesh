FROM alpine:3.8 AS builder

SHELL ["/bin/ash", "-eo", "pipefail", "-c"]

RUN apk add --no-cache \
    build-base \
    cmake \
    ninja \
    python2 \
    curl \
    tar \
    gzip

WORKDIR /src

RUN curl -fsSL \
    https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-6.0.0.tar.gz \
    | tar -xz

WORKDIR /src/llvm-project-llvmorg-6.0.0/build

RUN cmake -G Ninja ../llvm \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS=-static \
    -DBUILD_SHARED_LIBS=OFF \
    -DLLVM_BUILD_LLVM_DYLIB=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_ENABLE_ZLIB=OFF \
    -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DLLVM_EXTERNAL_CLANG_SOURCE_DIR=../clang

RUN ninja clang-format \
    && strip bin/clang-format

USER nobody

FROM alpine:3.20

RUN apk add --no-cache git bash \
    && git config --system --add safe.directory '*' \
    && addgroup -S app \
    && adduser -S -G app -u 10001 app

COPY --from=builder \
    /src/llvm-project-llvmorg-6.0.0/build/bin/clang-format \
    /usr/local/bin/clang-format

WORKDIR /work

USER 10001:app
