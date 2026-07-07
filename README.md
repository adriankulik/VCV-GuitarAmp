# Adrian Kulik Guitar Interface — VCV Rack 2 Plugin

Guitar pickup processor module for VCV Rack 2. Includes noise gate, overdrive/distortion/fuzz, 3-band EQ, and cabinet simulation.

---

## Prerequisites

- **VCV Rack 2 Free** installed (the app itself, not just the SDK)
- **Xcode Command Line Tools** — install via `xcode-select --install` if you haven't already
- **Rack 2 SDK** — download from [https://vcvrack.com/downloads](https://vcvrack.com/downloads)
  - Pick the macOS ARM64 version (Apple Silicon) or macOS x64 (Intel), matching your machine

---

## One-time Setup

### 1. Extract the SDK

Download the SDK zip (e.g. `Rack-SDK-2.6.6-mac-arm64.zip`) and extract it:

```bash
unzip ~/Downloads/Rack-SDK-2.6.6-mac-arm64.zip -d ~/rack-sdk-2
```

This creates `~/rack-sdk-2/Rack-SDK/` — that full path is your `RACK_DIR`.

---

## Building

From the project directory:

```bash
cd ~/Documents/Coding\ Projects/vcv\ rack\ guitar\ interface
export RACK_DIR=~/rack-sdk-2/Rack-SDK
make
```

This compiles `plugin.dylib` and a `build/` folder in the project directory.

---

## Installing into VCV Rack

After building, package and copy the plugin:

```bash
make install
cp dist/*.vcvplugin ~/Documents/Rack2/plugins-mac-arm64/
```

> **Why two steps?** `make install` packages the plugin into a `.vcvplugin` file but drops it in
> `~/Library/Application Support/Rack2/plugins-mac-arm64/` — a hidden system folder VCV Rack Free
> does not read. The manual `cp` puts it where VCV Rack Free actually looks:
> `~/Documents/Rack2/plugins-mac-arm64/`.

Then restart VCV Rack 2. Right-click the rack → Add Module → search **Guitar FX**.

### Shortcut: build script

A `build.sh` script is included in the project root. Make it executable once:

```bash
chmod +x ~/Documents/Coding\ Projects/vcv\ rack\ guitar\ interface/build.sh
```

Then run it from anywhere:

```bash
~/Documents/Coding\ Projects/vcv\ rack\ guitar\ interface/build.sh
```

Or from inside the project directory:

```bash
./build.sh
```

The script is equivalent to:

```bash
cd ~/Documents/Coding\ Projects/vcv\ rack\ guitar\ interface && \
RACK_DIR=~/rack-sdk-2/Rack-SDK make && \
cp dist/*.vcvplugin ~/Documents/Rack2/plugins-mac-arm64/
```

---

## File Layout

```
vcv rack guitar interface/
├── README.md
├── Makefile
├── plugin.json          — plugin manifest (slug, version, module list)
├── res/
│   └── GuitarFX.svg     — panel graphic (placeholder, to be designed)
└── src/
    ├── plugin.hpp        — shared types and externs
    ├── plugin.cpp        — plugin entry point / module registration
    └── GuitarFX.cpp      — all DSP (biquad filters, waveshaper, gate, cab sim) + UI layout
```

**Build artifacts** (not committed, safe to delete and regenerate):
- `build/` — compiled object files
- `dist/` — staged plugin bundle
- `plugin.dylib` — raw compiled binary

---

## Signal Chain

```
Audio In → Noise Gate → Waveshaper (Overdrive / Distortion / Fuzz)
         → 3-Band EQ (Bass / Mid / Treble)
         → Cabinet Sim (filter approximation)
         → Volume → Audio Out
                  → Gate CV Out (10V when gate is open)
```
