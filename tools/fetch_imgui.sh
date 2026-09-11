#!/bin/bash
# Re-fetches the pinned Dear ImGui release into thirdparty/imgui.
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
