#!/usr/bin/env bash
# The Linux release: XenonLiveLauncher-linux-x86_64.tar.zst and .AppImage,
# built on the ports' OLD BASE (Ubuntu 22.04, glibc 2.35) so the artifact runs
# on any distribution from 2022 on — including SteamOS.
#
# What goes in, and why:
#   bundled    SDL2 (the port's old-base build), libstdc++, libgcc_s
#   static     libcurl + OpenSSL + zlib, because distributions disagree about
#              libcurl's symbol versioning (Debian's is versioned, Fedora's is
#              not) and a dynamic one linked on either side fails on the other.
#              A static OpenSSL knows no CA directory but the build machine's,
#              so the launcher finds the system bundle at run time (app.cpp,
#              FindCaBundle) and names it through XLIVE_CA_FILE.
#   not bundled libc, libm, ld.so (never relocatable), Vulkan (not used)
#
# Usage:  tools/release_linux.sh [outDir]        default ~/Release/XenonLive_Launcher/<version>
#         XL_OLDBASE_SKIP_CURL=1                  reuse thirdparty/oldbase/curl
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=${XL_VERSION:-1.0.0}
OUT=${1:-$HOME/Release/XenonLive_Launcher/V$VERSION}
NAME=XenonLiveLauncher
XLIVE_ROOT=${XLIVE_ROOT:-$HOME/GithubRepo/XenonLive}
CW_ROOT=${CW_ROOT:-$HOME/GithubRepo/Dead_Rising_2_Case_West_Xenon_Recomp}
SDL2PFX=${XL_SDL2_PREFIX:-$CW_ROOT/thirdparty/oldbase/sdl2}
RUNTIME=${XL_APPIMAGE_RUNTIME:-$CW_ROOT/thirdparty/appimage/runtime-x86_64}
IMAGE=${XL_OLDBASE_IMAGE:-xenonlive-launcher-oldbase:jammy}
BASE_IMAGE=${XL_BASE_IMAGE:-cw-oldbase:jammy}
CURL_VERSION=8.14.1
CURL_SHA256=f4619a1e2474c4bbfedc88a7c2191209c8334b48fa1f4e53fd584cc12e9120dd
OB=$ROOT/thirdparty/oldbase
fail() { echo "FAIL: $*" >&2; exit 1; }

command -v podman >/dev/null || fail "podman not installed"
[ -f "$SDL2PFX/lib/libSDL2-2.0.so.0" ] || fail "no old-base SDL2 at $SDL2PFX (run Case West's tools/release_build_oldbase.sh once)"
[ -f "$RUNTIME" ] || fail "no AppImage runtime at $RUNTIME"
[ -f "$XLIVE_ROOT/client/CMakeLists.txt" ] || fail "no XenonLive checkout at $XLIVE_ROOT"
podman image exists "$BASE_IMAGE" || fail "no $BASE_IMAGE (Case West's tools/release_build_oldbase.sh builds it)"
if ! podman image exists "$IMAGE"; then
    echo "==> building $IMAGE from $BASE_IMAGE"
    podman build -t "$IMAGE" --build-arg "BASE=$BASE_IMAGE" -f "$ROOT/tools/release/oldbase/Containerfile" "$ROOT/tools/release/oldbase"
fi

mkdir -p "$OB/work" "$OB/home" "$OUT"
if [ ! -f "$OB/work/curl-$CURL_VERSION.tar.xz" ]; then
    curl -fsSL -o "$OB/work/curl-$CURL_VERSION.tar.xz" "https://curl.se/download/curl-$CURL_VERSION.tar.xz"
fi
echo "$CURL_SHA256  $OB/work/curl-$CURL_VERSION.tar.xz" | sha256sum -c - >/dev/null || fail "curl tarball checksum"

# Everything below runs INSIDE the container, with the repos mounted at their
# own absolute paths so RPATHs and ldd agree on both sides.
RUN=(podman run --rm -i
     -v "$ROOT:$ROOT:Z"
     -v "$XLIVE_ROOT:$XLIVE_ROOT:ro,Z"
     -v "$SDL2PFX:$SDL2PFX:ro,Z"
     -v "$OUT:$OUT:Z"
     -e HOME="$OB/home" -e TAR_OPTIONS=--no-same-owner -e XL_OLDBASE_SKIP_CURL="${XL_OLDBASE_SKIP_CURL:-}"
     -w "$ROOT" "$IMAGE" bash -s)
"${RUN[@]}" <<INNER
set -euo pipefail
OB="$OB"; ROOT="$ROOT"; OUT="$OUT"; NAME="$NAME"; VERSION="$VERSION"
SDL2PFX="$SDL2PFX"; XLIVE_ROOT="$XLIVE_ROOT"; CURL_VERSION="$CURL_VERSION"

if [ -z "\${XL_OLDBASE_SKIP_CURL:-}" ] || [ ! -f "\$OB/curl/lib/libcurl.a" ]; then
    echo "==> libcurl \$CURL_VERSION, static, OpenSSL, HTTP only"
    rm -rf "\$OB/work/curl-src" "\$OB/curl"
    mkdir -p "\$OB/work/curl-src"
    tar xf "\$OB/work/curl-\$CURL_VERSION.tar.xz" -C "\$OB/work/curl-src" --strip-components=1
    cmake -S "\$OB/work/curl-src" -B "\$OB/work/curl-build" -G Ninja \\
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \\
        -DCMAKE_INSTALL_PREFIX="\$OB/curl" \\
        -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=OFF \\
        -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \\
        -DCURL_USE_OPENSSL=ON -DOPENSSL_USE_STATIC_LIBS=ON \\
        -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_NGHTTP2=OFF \\
        -DUSE_LIBIDN2=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_USE_GSSAPI=OFF \\
        -DCURL_DISABLE_LDAP=ON -DHTTP_ONLY=ON \\
        -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none -DCURL_CA_FALLBACK=ON >/dev/null
    cmake --build "\$OB/work/curl-build" -j\$(nproc) >/dev/null
    cmake --install "\$OB/work/curl-build" >/dev/null
fi

echo "==> the launcher, Release, on the old base"
B="\$ROOT/build-release-oldbase"
rm -rf "\$B"
cmake -S "\$ROOT" -B "\$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \\
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \\
    -DXLIVE_ROOT="\$XLIVE_ROOT" -DXENONLIVE_RELEASE=ON -DXL_VERSION="\$VERSION" \\
    -DCMAKE_PREFIX_PATH="\$SDL2PFX;\$OB/curl" -DOPENSSL_USE_STATIC_LIBS=ON >/dev/null
cmake --build "\$B" -j\$(nproc)

echo "==> stage"
STAGE="\$OUT/.stage-linux/\$NAME"
rm -rf "\$OUT/.stage-linux"
mkdir -p "\$STAGE/lib"
cp "\$B/xenonlive_launcher" "\$STAGE/"
# SDL2 from the old-base prefix (the RPATH is \$ORIGIN/lib, so ldd cannot
# resolve it in the build tree), libstdc++ and libgcc_s from what ldd resolves
# in THIS container — the old base's copies.
cp -L "\$SDL2PFX/lib/libSDL2-2.0.so.0" "\$STAGE/lib/"
ldd "\$STAGE/xenonlive_launcher" | awk '/libstdc\\+\\+|libgcc_s/ { print \$3 }' | while read -r so; do
    cp -L "\$so" "\$STAGE/lib/"
done
strip --strip-unneeded "\$STAGE/xenonlive_launcher" "\$STAGE"/lib/*.so* 2>/dev/null || true
cp "\$ROOT/README.md" "\$STAGE/"
cp "\$ROOT/tools/release/THIRD_PARTY.md" "\$STAGE/"
# The font's licence must travel with the font (SIL OFL 1.1).
cp "\$ROOT/thirdparty/selawik/LICENSE.txt" "\$STAGE/LICENSE-Selawik.txt"
echo "    linked libs:"; ldd "\$STAGE/xenonlive_launcher" | sed 's/^/      /'
echo "    glibc floor: \$(objdump -T "\$STAGE/xenonlive_launcher" | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1)"
if ldd "\$STAGE/xenonlive_launcher" | grep -qE 'libcurl|libssl|libcrypto|libz\\.so'; then
    echo "FAIL: curl, OpenSSL or zlib is linked dynamically" >&2; exit 1
fi
echo "    curl, OpenSSL: static (absent from ldd)"
if ldd "\$STAGE/xenonlive_launcher" | grep -v "\$STAGE" | grep -vE 'linux-vdso|libc\\.so|libm\\.so|ld-linux'; then
    echo "FAIL: an unbundled library, above" >&2; exit 1
fi

echo "==> \$NAME-linux-x86_64.tar.zst"
(cd "\$OUT/.stage-linux" && rm -f "\$OUT/\$NAME-linux-x86_64.tar.zst" && tar --zstd -cf "\$OUT/\$NAME-linux-x86_64.tar.zst" "\$NAME")
INNER

echo "==> AppImage"
APPDIR=$OUT/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr"
cp -r "$OUT/.stage-linux/$NAME/." "$APPDIR/usr/"
cat > "$APPDIR/AppRun" <<'SH'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/usr/xenonlive_launcher" "$@"
SH
chmod +x "$APPDIR/AppRun"
cat > "$APPDIR/xenonlive_launcher.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=XenonLive
Comment=Sign in, friends, invites, achievements, and the XenonRecomp ports
Exec=xenonlive_launcher
Icon=xenonlive_launcher
Terminal=false
Categories=Game;
DESK
cp "$ROOT/tools/release/icon/xenonlive_launcher.png" "$APPDIR/xenonlive_launcher.png"
ln -sf xenonlive_launcher.png "$APPDIR/.DirIcon"
IMG=$OUT/$NAME-linux-x86_64.AppImage
SQ=$OUT/$NAME.squashfs
rm -f "$SQ" "$IMG"
mksquashfs "$APPDIR" "$SQ" -root-owned -noappend -no-xattrs -comp zstd -Xcompression-level 19 -quiet
cat "$RUNTIME" "$SQ" > "$IMG"
rm -f "$SQ"
rm -rf "$APPDIR"
chmod +x "$IMG"

echo "==> self-check: the AppImage starts headless (no FUSE path)"
T=$(mktemp -d)
if ( cd "$T" && timeout 8 env HOME="$T" XLIVE_DATA_DIR="$T/data" SDL_VIDEODRIVER=offscreen XENONLIVE_SCREENSHOT="$T/shot.bmp" APPIMAGE_EXTRACT_AND_RUN=1 "$IMG" >"$T/log" 2>&1; [ -f "$T/shot.bmp" ] ); then
    echo "    ok: drew a frame ($(stat -c%s "$T/shot.bmp") bytes)"
else
    echo "    FAIL: no frame; log:"; sed 's/^/      /' "$T/log" | tail -20; exit 1
fi
rm -rf "$T"

rm -rf "$OUT/.stage-linux"
(cd "$OUT" && sha256sum $(ls "$NAME"-*.tar.zst "$NAME"-*.AppImage "$NAME"-*.zip 2>/dev/null) > SHA256SUMS)
echo "==> done:"; ls -la "$OUT" | sed 's/^/    /'; sed 's/^/    /' "$OUT/SHA256SUMS"
