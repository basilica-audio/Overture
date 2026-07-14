# Tight Boost

*The 808 boost — tighten your low end before the gain hits.*

[![CI](https://github.com/yves-vogl/tight-boost/actions/workflows/ci.yml/badge.svg)](https://github.com/yves-vogl/tight-boost/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Tight Boost is pre-1.0 and under active development. There are no built binaries or releases yet — building from source is currently the only way to run it. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Tight Boost is a TS-808-style tight overdrive/boost built on JUCE 8, aimed at the pre-amp tightening stage metal guitarists run in front of a high-gain amp: it strips low end before the clipper (the "808 boost" trick) so palm mutes stay tight instead of farting out into the gain stage, then drives an oversampled asymmetric soft clipper for the actual overdrive character.

## Features (v0.1 scope)

- **Tight** - high-pass pre-emphasis, 20 Hz - 400 Hz (default 150 Hz), removes low end before the clipper
- **Drive** - 0 - 40 dB of gain into the clipper
- **Asymmetric soft clip** - tanh-based clipper with a fixed asymmetry bias, run inside 4x oversampling to keep aliasing out of the clipped signal
- **Tone** - post-clip low-pass, 1 kHz - 8 kHz (default 5 kHz), tames fizz without touching the fundamental
- **Level** - output trim, -24 dB to +24 dB
- **Mix** - dry/wet, with the dry path delay-compensated against the oversampling latency so Mix at 0% is a sample-accurate passthrough
- Full state save/recall via `AudioProcessorValueTreeState`

## Signal flow

```
Input --> Tight (HPF, 20-400 Hz) --> Drive (0-40 dB) --> [4x oversampled] Asym soft clip
                                                                  |
      Output <-- Mix <-- Level (output trim) <-- Tone (LPF, 1-8 kHz) <--+
        ^
        |
   delay-compensated dry path
```

See [`docs/architecture.md`](docs/architecture.md) for the full breakdown, including the oversampling/latency-compensation strategy and parameter smoothing.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M0 | Bootstrap - project skeleton, CI, docs | Done |
| M1 | DSP core - Tight/Drive/clip/Tone/Level/Mix signal path, oversampling + latency compensation, unit tests | Done |
| M2 | Custom GUI | Planned |
| M3 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
<!-- ==END BODY== -->

## Installation

No pre-built binaries are published yet (see the work-in-progress notice above). Once releases begin, installation will follow the standard plugin locations:

**macOS**

| Format | Path |
|---|---|
| AU (Component) | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

If Logic Pro doesn't pick up the plugin after installing, force a rescan by resetting the AU cache:

```sh
killall -9 AudioComponentRegistrar
auval -a
```

**Windows**

| Format | Path |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |

## Building from source

Requires JUCE 8.0.14, C++20, and CMake ≥ 3.24. See [`docs/building.md`](docs/building.md) for full prerequisites and step-by-step build/test commands for macOS and Windows.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Tight Boost is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Tight Boost is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.
