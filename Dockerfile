# syntax=docker/dockerfile:1
#
# Reproducible build, test and documentation environment for Astra2D.
#
#   docker build -t astra2d .
#   docker run --rm astra2d                 # build with ASan + UBSan and run the tests (default)
#   docker run --rm astra2d all             # everything: lint, tests, Valgrind, release, docs,
#                                           # example smoke tests
#   docker run --rm -v "$PWD/site:/out" astra2d docs   # generate the docs into ./site
#
# See scripts/docker-entrypoint.sh for all commands. OpenGL works without a GPU through Mesa's
# llvmpipe software renderer on an Xvfb virtual display.

FROM archlinux:base-devel

# Arch is a rolling release. Pinning the package mirror to a dated snapshot of the Arch Linux Archive
# makes the image reproducible: the same compiler and library versions on every build.
# Bump the date to update all tools at once.
ARG ARCH_SNAPSHOT=2026/11/02

RUN echo "Server = https://archive.archlinux.org/repos/${ARCH_SNAPSHOT}/\$repo/os/\$arch" \
        > /etc/pacman.d/mirrorlist \
 && pacman -Syyuu --noconfirm --needed \
        gcc clang meson ninja pkgconf \
        glfw glm spdlog nlohmann-json gtest \
        mesa xorg-server-xvfb xorg-xauth \
        doxygen graphviz valgrind \
 && pacman -Scc --noconfirm

ENV LIBGL_ALWAYS_SOFTWARE=1 \
    GALLIUM_DRIVER=llvmpipe \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

WORKDIR /astra2d
COPY . .

# Build once at image build time, so a broken build fails `docker build` and `docker run` starts fast.
RUN meson setup build -Db_sanitize=address,undefined \
 && ninja -C build \
 && chmod -R a+rwX /astra2d

ENTRYPOINT ["scripts/docker-entrypoint.sh"]
CMD ["test"]
