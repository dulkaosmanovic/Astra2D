#!/bin/bash

set -e

echo "Installing dependencies..."

sudo pacman -Syu --needed \
    base-devel \
    gcc \
    meson \
    ninja \
    git \
    glfw \
    glm \
    spdlog \
    gtest \
    mesa \
    doxygen \
    graphviz \
    valgrind \
    clang \
    clang-tools-extra

echo "Setting up Meson build..."

meson setup build -Db_sanitize=address,undefined || true

echo "Done!"
