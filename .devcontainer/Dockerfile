FROM ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    clang-18 lld-18 llvm-18 ninja-build cmake python3 git \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxfixes-dev libxrender-dev libwayland-dev libxkbcommon-dev \
    libdecor-0-dev libglib2.0-dev libpulse-dev libasound2-dev \
    && ln -sf /usr/bin/clang-18 /usr/local/bin/clang \
    && ln -sf /usr/bin/clang++-18 /usr/local/bin/clang++ \
    && ln -sf /usr/bin/ld.lld-18 /usr/local/bin/ld.lld \
    && ln -sf /usr/bin/llvm-ar-18 /usr/local/bin/llvm-ar \
    && ln -sf /usr/bin/llvm-ranlib-18 /usr/local/bin/llvm-ranlib \
    && ln -sf /usr/bin/lld-link-18 /usr/local/bin/lld-link \
    && rm -rf /var/lib/apt/lists/*
