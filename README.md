# Guitar Amp & Effects Plugin

Guitar pickup processor module for VCV Rack 2. Includes noise gate, overdrive/distortion/fuzz, 3-band EQ, cabinet simulation, and shimmer reverb.

*Watch the demo video below:*<br>

https://github.com/user-attachments/assets/86d8d1fe-ac6b-4cb8-9e84-8b27e1f953cc

---

## Installation

1. Download the latest `.vcvplugin` release from the [Releases page](https://github.com/adriankulik/vcv-rack-guitar-interface/releases).
2. Open VCV Rack 2.
3. In the top menu, go to **Help > Open user folder** (or locate your Rack 2 folder manually, e.g., `~/Documents/Rack2` on macOS/Windows, or `~/.Rack2` on Linux).
4. Move the downloaded `.vcvplugin` file into the `plugins-mac-arm64` / `plugins-mac-x64` / `plugins-win-x64` (depending on your OS) folder within your VCV Rack user folder.
5. Restart VCV Rack 2.
6. Right-click the rack → Add Module → Search for **Guitar Amp & Effects**.

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

Then restart VCV Rack 2. Right-click the rack → Add Module → search **Guitar Amp & Effects**.

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

The script is equivalent to running `make install` and manually copying the contents of `dist/$SLUG/` directly into `~/Documents/Rack2/plugins-mac-arm64/$SLUG/`. It also features a colorful output to track progress.

---

## File Layout

```
vcv rack guitar interface/
├── README.md
├── Makefile
├── build.sh             — colorful build script
├── plugin.json          — plugin manifest (slug, version, module list)
├── res/
│   ├── GuitarAmp.svg    — main UI panel graphic
│   └── LogoLight.svg    — dynamic logo glow layer
└── src/
    ├── plugin.hpp       — shared types and externs
    ├── plugin.cpp       — plugin entry point / module registration
    ├── GuitarAmp.cpp    — module entry point & UI layout
    ├── Shimmer.hpp      — stereo shimmer reverb DSP
    ├── CabinetSim.hpp   — cabinet simulator DSP
    ├── Drive.hpp        — overdrive/distortion/fuzz DSP
    ├── NoiseGate.hpp    — noise gate DSP
    └── Biquad.hpp       — biquad filter implementation
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
         → Shimmer Reverb (Stereo Pitch-shifted feedback delay + Tone filter)
         → Volume → Audio Out (Left / Right)
                  → Gate CV Out (10V when gate is open)
```

---

<img width="100%" alt="Screenshot 2026-08-16 at 00 22 36" src="https://github.com/user-attachments/assets/e257b546-45f9-4ebf-b31a-b21f7fce1fa9" />
