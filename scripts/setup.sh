#!/bin/bash

set -euo pipefail
cd "$(dirname "$0")/.."

echo "Installing dependencies..."

# -S --needed only installs what is missing; it does not upgrade the whole system.
# The clang package also provides clang-format, clang-tidy and run-clang-tidy.
sudo pacman -S --needed \
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
    clang

echo "Fetching git submodules..."

git submodule update --init --recursive

echo "Setting up Meson build..."

if [[ -f build/build.ninja ]]; then
    echo "build/ is already configured, skipping."
else
    meson setup build -Db_sanitize=address,undefined
fi

echo "Done!"
