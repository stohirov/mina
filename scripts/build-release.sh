#!/bin/sh
# Build a self-contained ./mina and package it in dist/. SDL3 and FreeType are
# built from source and linked in, so the result needs nothing else installed.
# GitHub Actions runs this script too, so it can be tried locally the same way.
set -eu

SDL_VERSION=3.4.18
SDL_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
FREETYPE_VERSION=2-14-3
FREETYPE_SHA256=dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build"
DEPS="$BUILD/deps"
JOBS=$(getconf _NPROCESSORS_ONLN)

case "$(uname -s)" in
    Darwin)
        PLATFORM=macos-universal
        RELEASE_CC="cc -arch arm64 -arch x86_64"
        CMAKE_ARCHS="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
        export MACOSX_DEPLOYMENT_TARGET=11.0
        ;;
    Linux)
        PLATFORM="linux-$(uname -m)"
        RELEASE_CC=cc
        CMAKE_ARCHS=
        ;;
    *)
        echo "Unsupported system: $(uname -s)" >&2
        exit 1
        ;;
esac

# Download a source archive, check it, and unpack it into build/src/$name.
fetch() {
    name=$1
    url=$2
    sha256=$3
    mkdir -p "$BUILD/src/$name"
    curl -fsSL "$url" -o "$BUILD/src/$name.tar.gz"
    echo "$sha256  $BUILD/src/$name.tar.gz" | shasum -a 256 -c -
    tar -xzf "$BUILD/src/$name.tar.gz" -C "$BUILD/src/$name" --strip-components=1
}

# $CMAKE_ARCHS is empty on Linux, so it is left unquoted on purpose.
build_cmake() {
    name=$1
    shift
    cmake -S "$BUILD/src/$name" -B "$BUILD/src/$name/out" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        $CMAKE_ARCHS "$@"
    cmake --build "$BUILD/src/$name/out" -j "$JOBS"
    cmake --install "$BUILD/src/$name/out"
}

if [ ! -f "$DEPS/lib/libSDL3.a" ]; then
    fetch sdl "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz" "$SDL_SHA256"
    build_cmake sdl -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF \
        -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF
fi

if [ ! -f "$DEPS/lib/libfreetype.a" ]; then
    fetch freetype "https://github.com/freetype/freetype/archive/refs/tags/VER-$FREETYPE_VERSION.tar.gz" "$FREETYPE_SHA256"
    build_cmake freetype -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON \
        -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
fi

DEP_CFLAGS="-I$DEPS/include -I$DEPS/include/freetype2"
SDL_LIBS=$(sed -n 's/^Libs[.a-z]*: *//p' "$DEPS/lib/pkgconfig/sdl3.pc" | sed "s|\${libdir}|$DEPS/lib|g" | tr '\n' ' ')
DEP_LIBS="$SDL_LIBS $DEPS/lib/libfreetype.a"

cd "$ROOT"
make clean
make test DEP_CFLAGS="$DEP_CFLAGS" DEP_LIBS="$DEP_LIBS"

make clean
make mina CC="$RELEASE_CC" CFLAGS="-std=gnu11 -Wall -Wextra -O2" \
    DEP_CFLAGS="$DEP_CFLAGS" DEP_LIBS="$DEP_LIBS"

# Fail here, rather than for whoever downloads it, if a library was not linked in.
case "$PLATFORM" in
    macos-*)
        if otool -L mina | grep -E 'libSDL3|libfreetype|/opt/homebrew|/usr/local'; then
            echo "mina still depends on the libraries above" >&2
            exit 1
        fi
        ;;
    *)
        if ldd mina | grep -E 'libSDL3|libfreetype'; then
            echo "mina still depends on the libraries above" >&2
            exit 1
        fi
        ;;
esac

PACKAGE="mina-$PLATFORM"
rm -rf "$ROOT/dist"
mkdir -p "$ROOT/dist/$PACKAGE"
cp mina README.md LICENSE THIRD_PARTY.md "$ROOT/dist/$PACKAGE/"
tar -czf "$ROOT/dist/$PACKAGE.tar.gz" -C "$ROOT/dist" "$PACKAGE"
rm -rf "$ROOT/dist/$PACKAGE"
echo "Created dist/$PACKAGE.tar.gz"
