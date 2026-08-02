#!/bin/bash
set -e

cd "$(dirname "$0")"

# ANSI Color Codes
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${CYAN} Building VCV Rack Plugin ${NC}"
echo -e "${BLUE}========================================${NC}"

# ── 1. Compile and package ───────────────────────────────────────────────────
echo -e "${YELLOW}[1/2] Compiling and packaging...${NC}"
RACK_DIR=~/rack-sdk-2/Rack-SDK make install

# ── 2. Copy into the live Rack plugins directory ─────────────────────────────
echo -e "${YELLOW}[2/2] Copying into live Rack plugins directory...${NC}"
SLUG=$(jq -r '.slug' plugin.json)
DEST=~/Documents/Rack2/plugins-mac-arm64/$SLUG
mkdir -p "$DEST"
cp dist/$SLUG/plugin.dylib "$DEST/"
cp dist/$SLUG/plugin.json  "$DEST/"
cp -r dist/$SLUG/res        "$DEST/"

echo -e "${GREEN}✨ Done. Restart VCV Rack to load the updated plugin! ✨${NC}"
