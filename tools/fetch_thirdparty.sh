#!/bin/bash
# Re-fetches the pinned third-party code into thirdparty/: Dear ImGui, miniz, stb_image.
# Only needed if thirdparty/ is ever lost; the checkout vendors these files.
set -euo pipefail
VERSION=1.91.9b
SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
URL="https://github.com/ocornut/imgui/archive/refs/tags/v${VERSION}.tar.gz"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/thirdparty/imgui"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

curl -sSL -o "$TMP/imgui.tar.gz" "$URL"
echo "$SHA256  $TMP/imgui.tar.gz" | sha256sum -c -
tar xzf "$TMP/imgui.tar.gz" -C "$TMP"
SRC="$TMP/imgui-$VERSION"

mkdir -p "$DEST/backends"
cp "$SRC"/imgui.cpp "$SRC"/imgui.h "$SRC"/imgui_demo.cpp "$SRC"/imgui_draw.cpp \
   "$SRC"/imgui_internal.h "$SRC"/imgui_tables.cpp "$SRC"/imgui_widgets.cpp \
   "$SRC"/imstb_rectpack.h "$SRC"/imstb_textedit.h "$SRC"/imstb_truetype.h \
   "$SRC"/imconfig.h "$SRC"/LICENSE.txt "$DEST/"
cp "$SRC"/backends/imgui_impl_sdl2.cpp "$SRC"/backends/imgui_impl_sdl2.h \
   "$SRC"/backends/imgui_impl_sdlrenderer2.cpp "$SRC"/backends/imgui_impl_sdlrenderer2.h \
   "$DEST/backends/"
echo "Dear ImGui $VERSION vendored into $DEST"

# miniz 3.0.2 (MIT), the zip reader the Windows install path uses. Same
# reason it is vendored: the release zip format will not change, and neither
# should the code that reads it.
MINIZ_VERSION=3.0.2
MINIZ_SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
curl -sSL -o "$TMP/miniz.zip" \
    "https://github.com/richgel999/miniz/releases/download/${MINIZ_VERSION}/miniz-${MINIZ_VERSION}.zip"
echo "$MINIZ_SHA256  $TMP/miniz.zip" | sha256sum -c -
mkdir -p "$TMP/miniz" "$ROOT/thirdparty/miniz"
python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$TMP/miniz.zip" "$TMP/miniz"
cp "$TMP/miniz/miniz.c" "$TMP/miniz/miniz.h" "$TMP/miniz/LICENSE" "$ROOT/thirdparty/miniz/"
echo "miniz $MINIZ_VERSION vendored into $ROOT/thirdparty/miniz"

# stb_image.h v2.30 (public domain / MIT), the PNG decoder for achievement
# tiles. Pinned to a commit of nothings/stb; the licence is at the end of the
# file itself.
STB_COMMIT=2c980bb59875b0d32144a71867fbdebb2f77cd20
STB_SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
mkdir -p "$ROOT/thirdparty/stb"
curl -sSL -o "$TMP/stb_image.h" "https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/stb_image.h"
echo "$STB_SHA256  $TMP/stb_image.h" | sha256sum -c -
cp "$TMP/stb_image.h" "$ROOT/thirdparty/stb/"
echo "stb_image.h vendored into $ROOT/thirdparty/stb"
