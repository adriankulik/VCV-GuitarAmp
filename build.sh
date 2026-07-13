#!/bin/bash
set -e

cd "$(dirname "$0")"

# ── 2. Compile and package ───────────────────────────────────────────────────
RACK_DIR=~/rack-sdk-2/Rack-SDK make install

# ── 3. Copy into the live Rack plugins directory ─────────────────────────────
SLUG=$(jq -r '.slug' plugin.json)
DEST=~/Documents/Rack2/plugins-mac-arm64/$SLUG
mkdir -p "$DEST"
cp dist/$SLUG/plugin.dylib "$DEST/"
cp dist/$SLUG/plugin.json  "$DEST/"
cp -r dist/$SLUG/res        "$DEST/"

echo "Done. Restart VCV Rack to load the updated plugin."
