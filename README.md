<p align="center"><img src="docs/assets/icon.png" alt="Overture icon" width="160"/></p>

# Overture

*The 808 boost — tighten your low end before the gain hits.*

[![CI](https://github.com/basilica-audio/overture/actions/workflows/ci.yml/badge.svg)](https://github.com/basilica-audio/overture/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Overture is pre-1.0 and under active development. Binaries for macOS and Windows are available from the [Releases](../../releases) page (macOS builds are signed with a Developer ID certificate, notarized and stapled); building from source works too. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Overture is a TS-808-style tight overdrive/boost built on JUCE 8, aimed at the pre-amp tightening stage metal guitarists run in front of a high-gain amp: it strips low end before the clipper (the "808 boost" trick) so palm mutes stay tight instead of farting out into the gain stage, then drives an oversampled, selectable-voicing clipper for the actual overdrive character. See [`docs/manual.md`](docs/manual.md) for the full user manual (signal flow, every parameter explained, and usage tips).

## Features

- **Tight** - high-pass pre-emphasis, 20 Hz - 400 Hz (default 100 Hz), removes low end before the clipper
- **Drive** - 0 - 40 dB of gain into the clipper (default 3 dB)
- **Bite** - frequency-dependent gain *inside* the drive-to-clipper path (0-100%, default 65%) - bass is clipped less than treble, reproducing the reference circuit's own tightening mechanism rather than approximating it with a separate filter
- **Knee Soften** - drive-dependent knee softening (0-100%, default 40%), applies to all three voicings including Hard Clip
- **Asymmetry** - exposes the Asymmetric voicing's internal bias as a 0-100% control (default 40%, reproducing v0.1's fixed bias)
- **Voicing** - Asymmetric (biased tanh, the original "808 boost" character), Soft Symmetric (unbiased tanh), or Hard Clip (straight clamp), run inside oversampling to keep aliasing out of the clipped signal
- **Bite Tilt** - post-clip bidirectional shelf around a ~3 kHz corner (-100% to +100%, default 0%/flat), replacing v0.1's cut-only Tone - darkens *or* brightens
- **Level** - output trim, -24 dB to +24 dB
- **Mix** - dry/wet, with the dry path delay-compensated against the oversampling latency so Mix at 0% is a sample-accurate passthrough
- **Bypass** - host-visible soft bypass; keeps the oversampler running and latency reporting stable, crossfades instead of clicking
- **Oversampling** - 2x / 4x / 8x, selectable; takes effect on the next host re-initialisation (real-time-safe by design - see [`docs/manual.md`](docs/manual.md))
- **Presets** - eleven factory presets plus full user preset management (save/load/import/export/default), with a German-localised preset bar frame
- Full state save/recall via `AudioProcessorValueTreeState`, with tolerant migration of v0.1 sessions

## Signal flow

```
Input --> Tight (HPF, 20-400 Hz) --> Drive (0-40 dB) --> [oversampled]
            Bite shelf (~700 Hz, inside the drive-to-clipper path)
            --> Voicing clipper --> Knee Soften blend
                                                                  |
      Output <-- Mix <-- Level <-- Bite Tilt (+/-3 kHz shelf) <--+
        ^
        |
   delay-compensated dry path (also used by Bypass)
```

See [`docs/architecture.md`](docs/architecture.md) for the full engineering breakdown, including the oversampling/latency-compensation strategy and parameter smoothing, and [`docs/design-brief.md`](docs/design-brief.md)/[`docs/research-notes.md`](docs/research-notes.md) for the sourced rationale behind the v0.2.0 rework.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M0 | Bootstrap - project skeleton, CI, docs | Done |
| M1 | DSP completion & test coverage - Voicing/Bypass/Oversampling, 4th-order Tone stack, tuned defaults, broadened Catch2 suite | Done |
| M2 | Deep-dive DSP rework (Bite/Knee Soften/Asymmetry/Bite Tilt), presets & state recall, i18n frame | Done |
| M3 | Custom GUI & accessibility | Planned |
| M4 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
<!-- ==END BODY== -->

## Documentation

- [`docs/manual.md`](docs/manual.md) — the user manual: what every control does, and how to use it
- [`docs/presets.md`](docs/presets.md) — what each factory preset is for
- [`CHANGELOG.md`](CHANGELOG.md) — what shipped in each release
- [Overture on basilica-audio.github.io](https://basilica-audio.github.io/website/overture/) — the product page (English and German)

## Installation

Download the archive for your platform from the [Releases](../../releases) page and copy the bundles into the standard plugin locations:

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
