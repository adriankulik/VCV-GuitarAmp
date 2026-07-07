#!/bin/bash
set -e

cd "$(dirname "$0")"

# ── 1. Generate SVG from template + theme ───────────────────────────────────
source res/theme.sh

SVG=res/GuitarFX.svg
cp res/GuitarFX.svg.template "$SVG"

for var in BG_PANEL BG_GATE BG_DRIVE BG_EQ BG_CAB BG_SECTION_OPACITY \
           COL_TITLE COL_GATE COL_DRIVE COL_EQ COL_CAB \
           COL_KNOB COL_PORT COL_DIVIDER COL_SCREW \
           FONT_UI FONT_SIZE_TITLE FONT_SIZE_SECTION FONT_SIZE_LABEL; do
    sed -i '' "s|{{${var}}}|${!var}|g" "$SVG"
done

echo "Generated $SVG"

# ── 2. Compile and package ───────────────────────────────────────────────────
RACK_DIR=~/rack-sdk-2/Rack-SDK make install

# ── 3. Copy into the live Rack plugins directory ─────────────────────────────
DEST=~/Documents/Rack2/plugins-mac-arm64/AdrianKulikGuitarInterface
mkdir -p "$DEST"
cp dist/AdrianKulikGuitarInterface/plugin.dylib "$DEST/"
cp dist/AdrianKulikGuitarInterface/plugin.json  "$DEST/"
cp -r dist/AdrianKulikGuitarInterface/res        "$DEST/"

echo "Done. Restart VCV Rack to load the updated plugin."
