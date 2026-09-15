#!/usr/bin/env bash
# The Windows release: XenonLiveLauncher-windows-x86_64.zip, cross-compiled
# from Linux in a container (tools/release/mingw/Containerfile). No Windows
# machine is involved.
#
# What goes in: xenonlive_launcher.exe (libgcc, libstdc++ and winpthreads
# linked statically), SDL2.dll, the README and the third-party notice.
# libcurl is static and uses Schannel, the operating system's TLS, so no
# OpenSSL or CA bundle is shipped and the Windows certificate store is the
# trust store — the same as every browser on the machine.
#
# Usage:  tools/release_windows.sh [outDir]      default ~/Release/XenonLive_Launcher/<version>
#         XL_MINGW_SKIP_DEPS=1                    reuse thirdparty/mingw/{sdl2,curl}
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${XL_VERSION:-1.0.0}
OUT=${1:-$HOME/Release/XenonLive_Launcher/V$VERSION}
NAME=XenonLiveLauncher
XLIVE_ROOT=${XLIVE_ROOT:-$HOME/GithubRepo/XenonLive}
IMAGE=${XL_MINGW_IMAGE:-xenonlive-launcher-mingw:noble}
MW=$ROOT/thirdparty/mingw
SDL2_VERSION=2.32.10
SDL2_SHA256=${SDL2_SHA256:-}
CURL_VERSION=8.14.1
CURL_SHA256=f4619a1e2474c4bbfedc88a7c2191209c8334b48fa1f4e53fd584cc12e9120dd
fail() { echo "FAIL: $*" >&2; exit 1; }

command -v podman >/dev/null || fail "podman not installed"
[ -f "$XLIVE_ROOT/client/CMakeLists.txt" ] || fail "no XenonLive checkout at $XLIVE_ROOT"
if ! podman image exists "$IMAGE"; then
    echo "==> building $IMAGE"
    podman build -t "$IMAGE" -f "$ROOT/tools/release/mingw/Containerfile" "$ROOT/tools/release/mingw"
fi
mkdir -p "$MW/work" "$MW/home" "$OUT"
[ -f "$MW/work/curl-$CURL_VERSION.tar.xz" ] || curl -fsSL -o "$MW/work/curl-$CURL_VERSION.tar.xz" "https://curl.se/download/curl-$CURL_VERSION.tar.xz"
echo "$CURL_SHA256  $MW/work/curl-$CURL_VERSION.tar.xz" | sha256sum -c - >/dev/null || fail "curl tarball checksum"
if [ ! -f "$MW/work/SDL2-$SDL2_VERSION.tar.gz" ]; then
    # The ports' own cached copy first, the same source they ship.
    for d in cw cz; do
        [ -f "/var/tmp/$d-sdl2-build/SDL2-$SDL2_VERSION.tar.gz" ] && cp "/var/tmp/$d-sdl2-build/SDL2-$SDL2_VERSION.tar.gz" "$MW/work/" && break
    done
    [ -f "$MW/work/SDL2-$SDL2_VERSION.tar.gz" ] || curl -fsSL -o "$MW/work/SDL2-$SDL2_VERSION.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz"
fi

RUN=(podman run --rm -i
     -v "$ROOT:$ROOT:Z"
     -v "$XLIVE_ROOT:$XLIVE_ROOT:ro,Z"
     -v "$OUT:$OUT:Z"
     -e HOME="$MW/home" -e TAR_OPTIONS=--no-same-owner -e XL_MINGW_SKIP_DEPS="${XL_MINGW_SKIP_DEPS:-}"
     -w "$ROOT" "$IMAGE" bash -s)
"${RUN[@]}" <<INNER
set -euo pipefail
MW="$MW"; ROOT="$ROOT"; OUT="$OUT"; NAME="$NAME"; XLIVE_ROOT="$XLIVE_ROOT"; VERSION="$VERSION"
TC="\$ROOT/tools/release/mingw/toolchain.cmake"

if [ -z "\${XL_MINGW_SKIP_DEPS:-}" ] || [ ! -f "\$MW/sdl2/bin/SDL2.dll" ]; then
    echo "==> SDL2 $SDL2_VERSION for Windows"
    rm -rf "\$MW/work/sdl2-src" "\$MW/sdl2"
    mkdir -p "\$MW/work/sdl2-src"
    tar xzf "\$MW/work/SDL2-$SDL2_VERSION.tar.gz" -C "\$MW/work/sdl2-src" --strip-components=1
    cmake -S "\$MW/work/sdl2-src" -B "\$MW/work/sdl2-build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="\$TC" \\
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="\$MW/sdl2" \\
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF >/dev/null
    cmake --build "\$MW/work/sdl2-build" -j\$(nproc) >/dev/null
    cmake --install "\$MW/work/sdl2-build" >/dev/null
fi

if [ -z "\${XL_MINGW_SKIP_DEPS:-}" ] || [ ! -f "\$MW/curl/lib/libcurl.a" ]; then
    echo "==> libcurl $CURL_VERSION for Windows, static, Schannel, HTTP only"
    rm -rf "\$MW/work/curl-src" "\$MW/curl"
    mkdir -p "\$MW/work/curl-src"
    tar xf "\$MW/work/curl-$CURL_VERSION.tar.xz" -C "\$MW/work/curl-src" --strip-components=1
    cmake -S "\$MW/work/curl-src" -B "\$MW/work/curl-build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="\$TC" \\
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="\$MW/curl" \\
        -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=OFF \\
        -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \\
        -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \\
        -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \\
        -DCURL_DISABLE_LDAP=ON -DHTTP_ONLY=ON >/dev/null
    cmake --build "\$MW/work/curl-build" -j\$(nproc) >/dev/null
    cmake --install "\$MW/work/curl-build" >/dev/null
fi

echo "==> the launcher, Release, for Windows"
B="\$ROOT/build-release-mingw"
rm -rf "\$B"
cmake -S "\$ROOT" -B "\$B" -G Ninja -DCMAKE_TOOLCHAIN_FILE="\$TC" -DCMAKE_BUILD_TYPE=Release \\
    -DXL_MINGW_PREFIX="\$MW/sdl2;\$MW/curl" -DCMAKE_PREFIX_PATH="\$MW/sdl2;\$MW/curl" \\
    -DXLIVE_ROOT="\$XLIVE_ROOT" -DXENONLIVE_RELEASE=ON -DXL_VERSION="\$VERSION"
cmake --build "\$B" -j\$(nproc)

echo "==> stage"
STAGE="\$OUT/.stage-windows/\$NAME"
rm -rf "\$OUT/.stage-windows"
mkdir -p "\$STAGE"
cp "\$B/xenonlive_launcher.exe" "\$STAGE/"
cp "\$MW/sdl2/bin/SDL2.dll" "\$STAGE/"
x86_64-w64-mingw32-strip --strip-unneeded "\$STAGE/xenonlive_launcher.exe" "\$STAGE/SDL2.dll"
cp "\$ROOT/README.md" "\$STAGE/"
cp "\$ROOT/tools/release/THIRD_PARTY.md" "\$STAGE/"
# The font's licence must travel with the font (SIL OFL 1.1).
cp "\$ROOT/thirdparty/selawik/LICENSE.txt" "\$STAGE/LICENSE-Selawik.txt"
cp "\$ROOT/thirdparty/notocjk/LICENSE.txt" "\$STAGE/LICENSE-NotoSansCJK.txt"
echo "    DLL imports of the executable:"
x86_64-w64-mingw32-objdump -p "\$STAGE/xenonlive_launcher.exe" | grep 'DLL Name' | sed 's/^/      /'
(cd "\$OUT/.stage-windows" && rm -f "\$OUT/\$NAME-windows-x86_64.zip" && zip -qr "\$OUT/\$NAME-windows-x86_64.zip" "\$NAME")
rm -rf "\$OUT/.stage-windows"
(cd "\$OUT" && sha256sum \$(ls "\$NAME"-*.tar.zst "\$NAME"-*.tar.gz "\$NAME"-*.AppImage "\$NAME"-*.zip 2>/dev/null) > SHA256SUMS)
ls -la "\$OUT/\$NAME-windows-x86_64.zip"
INNER
