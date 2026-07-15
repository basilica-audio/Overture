<p align="center"><img src="docs/assets/icon.png" alt="Overture icon" width="160"/></p>

# Overture

*The 808 boost — tighten your low end before the gain hits.*

[![CI](https://github.com/basilica-audio/overture/actions/workflows/ci.yml/badge.svg)](https://github.com/basilica-audio/overture/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Overture is pre-1.0 and under active development. There are no built binaries or releases yet — building from source is currently the only way to run it. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Overture is a TS-808-style tight overdrive/boost built on JUCE 8, aimed at the pre-amp tightening stage metal guitarists run in front of a high-gain amp: it strips low end before the clipper (the "808 boost" trick) so palm mutes stay tight instead of farting out into the gain stage, then drives an oversampled, selectable-voicing clipper for the actual overdrive character. See [`docs/manual.md`](docs/manual.md) for the full user manual (signal flow, every parameter explained, and usage tips).

## Features (v0.1.0 scope)

- **Tight** - high-pass pre-emphasis, 20 Hz - 400 Hz (default 130 Hz), removes low end before the clipper
- **Drive** - 0 - 40 dB of gain into the clipper
- **Voicing** - Asymmetric (biased tanh, the original "808 boost" character), Soft Symmetric (unbiased tanh), or Hard Clip (straight clamp), run inside oversampling to keep aliasing out of the clipped signal
- **Tone** - post-clip low-pass, 1 kHz - 8 kHz (default 6 kHz), 4th-order (24 dB/oct) for effective fizz control without touching the fundamental
- **Level** - output trim, -24 dB to +24 dB
- **Mix** - dry/wet, with the dry path delay-compensated against the oversampling latency so Mix at 0% is a sample-accurate passthrough
- **Bypass** - host-visible soft bypass; keeps the oversampler running and latency reporting stable, crossfades instead of clicking
- **Oversampling** - 2x / 4x / 8x, selectable; takes effect on the next host re-initialisation (real-time-safe by design - see [`docs/manual.md`](docs/manual.md))
- Full state save/recall via `AudioProcessorValueTreeState`

## Signal flow

```
Input --> Tight (HPF, 20-400 Hz) --> Drive (0-40 dB) --> [oversampled] Voicing clipper
                                                                  |
      Output <-- Mix <-- Level (output trim) <-- Tone (4th-order LPF, 1-8 kHz) <--+
        ^
        |
   delay-compensated dry path (also used by Bypass)
```

See [`docs/architecture.md`](docs/architecture.md) for the full engineering breakdown, including the oversampling/latency-compensation strategy and parameter smoothing.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M0 | Bootstrap - project skeleton, CI, docs | Done |
| M1 | DSP completion & test coverage - Voicing/Bypass/Oversampling, 4th-order Tone stack, tuned defaults, broadened Catch2 suite | Done |
| M2 | Presets & state recall | Planned |
| M3 | Custom GUI & accessibility | Planned |
| M4 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
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

Overture is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Overture is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.
