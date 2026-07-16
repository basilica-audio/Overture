# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-07-16

Research-driven rework of the Drive -> Clipper -> Tone portion of the signal chain, sourced from published circuit analyses of the reference-class "tube-screamer-in-front-of-a-high-gain-amp" technique, a purpose-built commercial pedal's own documentation, and publicly reported artist workflows (`docs/research-notes.md`) - not measured against physical reference hardware. See `docs/design-brief.md` for the full reasoning behind every change below, and `docs/architecture.md`/`docs/manual.md` for the updated engineering/user-facing documentation.

### Added

- **`biteAmount`** ("Bite", 0-100%, default 65%): frequency-dependent gain *inside* the drive-to-clipper path - a fixed ~700 Hz low-shelf (anchored to the sourced ~720 Hz reference-circuit feedback corner) that reduces the drive reaching the clipper below the shelf, scaled by this control, so bass is clipped less than treble - the actual mechanism the reference circuit uses for "tightness", not an approximation via a separate pre-clip filter (that's still Tight's unchanged job). At 0%, the shelf is skipped entirely - bit-identical to v0.1's flat-gain clipper input.
- **`kneeSoften`** ("Knee Soften", 0-100%, default 40%): drive-dependent knee softening, blending each voicing's transfer function toward a softer-kneed variant, more pronounced the harder Drive is pushing the clipper (`src/dsp/KneeSoftening.h`). Applies to all three voicings, including Hard Clip, which has literally zero knee at 0% at any Drive level. At 0%, every voicing keeps its exact v0.1 fixed-knee shape, Drive-invariant.
- **`asymmetryAmount`** ("Asymmetry", 0-100%, default 40%): exposes the Asymmetric voicing's internal bias (fixed at `a = 0.2` in v0.1) as a control mapping to `a` in 0.0-0.5. Default 40% reproduces v0.1's fixed default exactly. Only affects the Asymmetric voicing, as in v0.1.
- **`biteTilt`** ("Bite Tilt", -100%..+100%, default 0%) replaces `tone`: a post-clip bidirectional shelf around a fixed ~3 kHz corner (reasoned proxy for the sourced ~3.2 kHz reference-circuit tone-tilt corner), darkening below 0% (subsuming v0.1's entire cut-only Tone range) and brightening above it - a capability v0.1's cut-only 4th-order low-pass entirely lacked. Flat (0%) is a true no-op (the filter is skipped, not given unity-gain coefficients).
- **State migration**: a v0.1-only saved session (`tone`, no `biteTilt`) loads without crashing; `tone`'s value is lossily mapped into an equivalent `biteTilt` position (1000 Hz -> -100%, 8000 Hz -> 0%, linear in between) - a best-effort, not mathematically exact, equivalence, since the two controls have genuinely different shapes. Every other new parameter falls back to its own v0.2.0 default on a legacy load.
- **M2 preset system** (`.scaffold/specs/preset-system-m2.md`, `src/presets/PresetManager.{h,cpp}` + `src/presets/PresetBar.{h,cpp}`, copied verbatim from the Nave pilot implementation): factory presets (embedded via BinaryData), user presets (`~/Library/Audio/Presets/Yves Vogl/Overture/` on macOS), dirty tracking, prev/next navigation, default resolution (user Default > factory Default > plain parameter defaults), single-file and zip-bank import/export. Nine factory presets ship (`docs/presets.md`): a certified **Default** plus eight use-case presets from `docs/design-brief.md`'s table (Clean Push, Classic Boost, Drop-Tune Tight, Smooth Push, Own Distortion, Fuzz-Adjacent Lead, Parallel Grit, De-Fizz Cleanup).
- **German localisation of the M2 preset frame** (`resources/i18n/de.txt`, `src/presets/Localisation.{h,cpp}`), selected automatically via `SystemStats::getUserLanguage()`. Core/DSP parameter names are never translated.
- App icon now wired into the built AU/VST3/Standalone via `ICON_BIG` (previously missing - Overture had shipped no patch release since the icon was added to the docs/README).
- Broadened Catch2 suite: new DSP-primitive tests (`tests/KneeSofteningTests.cpp`), asymmetry-sweep and v0.1 backward-compatibility regression tests (`tests/ClipperVoicingTests.cpp`), Bite frequency-dependent-gain and Knee Soften drive-dependence spectral proofs, Bite Tilt bidirectionality/no-op/subsumes-v0.1-Tone tests (`tests/EngineTests.cpp`), state-migration tests (`tests/StateTests.cpp`), the M2 preset-manager test suite (`tests/PresetManagerTests.cpp`), and i18n frame tests (`tests/LocalisationTests.cpp`).

### Changed

- **`tight` default: 130 Hz -> 100 Hz.** 100 Hz is the midpoint of the sourced, specifically-documented 80-120 Hz workflow sweet spot ("cutting everything below 80-120Hz... crucial for fast palm-muted riffs on low-tuned guitars"), rather than v0.1's reasoned-but-unsourced 130 Hz.
- **`drive` default: 8 dB -> 3 dB.** The best-documented canonical workflow for this technique is clipper drive "at or near zero" with Level/the amp doing the actual pushing (Misha Mansoor's documented approach; Horizon Devices' own Precision Drive manual: "start with drive near zero... slowly turn up to around 1-2 [of 10]"). 3 dB keeps the clipper audibly alive while sitting in that researched "mostly clean push" region.
- `docs/architecture.md`, `docs/manual.md`, and `README.md` updated to describe the full v0.2.0 signal path, parameter table, and factory presets.

### Removed

- **`tone`** (post-clip cut-only low-pass, 1-8 kHz): structurally replaced by `biteTilt` (see "Added" above). The parameter ID is retained internally, unregistered, solely for state-migration purposes (`ParamIDs::tone`'s docs) - a fresh instance never exposes it.

### Fixed

- **Audio-thread heap allocation in `OvertureEngine::process()` (#12):** the Tight HPF and both Tone LPF stages recomputed their `juce::dsp::IIR::Coefficients` every block via `Coefficients::makeHighPass`/`makeLowPass`, each of which heap-allocates a new ref-counted object (up to 6 allocations/6 deallocations per `processBlock()` call). Replaced with `juce::dsp::IIR::ArrayCoefficients` (stack-computed) written directly into the already-allocated `Coefficients` storage via a new `src/dsp/RealtimeCoefficients.h` helper - the same non-allocating pattern already used by sibling plugins Lancet and Twist Your Guts, now also covering the new Bite/Bite Tilt shelf coefficient updates. Regression-tested by `tests/AllocationTests.cpp`, which globally instruments `operator new`/`delete` and fails if `processBlock()`/`OvertureEngine::process()` ever allocates again.
- **No defensive clamp against oversized blocks in `processBlock()`/`OvertureEngine::process()` (#13):** a host handing `process()` more samples than the `spec.maximumBlockSize` declared to `prepare()` would overrun the oversampler's and `DryWetMixer`'s internal buffers, which only guard that invariant with a `jassert` (compiled out in Release builds). `OvertureEngine::process()` now defensively splits any oversized block into chunks of at most the prepared maximum before processing each chunk, matching the real, Release-safe chunking guard already used by sibling plugins Twist Your Guts and Miserere. Regression-tested by a `tests/EngineTests.cpp` case that feeds a 64x-oversized block through `process()` and verifies it produces bit-identical output to a correctly host-chunked reference run.
- **Release workflow (`.github/workflows/release.yml`):** added an idempotent `create-release` job (both platform jobs now `needs: create-release`) so `gh release upload` has a release object to attach assets to on a fresh tag push - previously both `release-macos` and `release-windows` failed with "release not found" since neither created the release itself.

## [0.1.0] - 2026-07-14

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- DSP core: initial working Overture signal path (Tight HPF, Drive, oversampled asymmetric soft clipper, Tone LPF, Level, Mix) with unit tests.
- Host-visible **Bypass** parameter (`getBypassParameter()`). Reuses the existing delay-compensated Mix/DryWetMixer path internally, so the oversampler keeps running and the plugin's reported latency never changes on a bypass toggle; engaging/disengaging crossfades smoothly instead of clicking.
- **Voicing** parameter selecting the clipper nonlinearity: Asymmetric (the original biased tanh, default), Soft Symmetric (unbiased tanh), or Hard Clip (straight clamp). New `src/dsp/ClipperVoicing.h`.
- **Oversampling** parameter (2x/4x/8x, default 4x) selecting the oversampling factor. Takes effect on the next `prepareToPlay()` rather than instantaneously, by design - reconstructing the oversampler allocates, which must never happen on the audio thread.
- Tone stage refined from a single 2nd-order low-pass to a cascaded 4th-order Butterworth low-pass (24 dB/octave), for materially more effective post-clipper fizz control at the same cutoff.
- Tuned default parameter values for a real "boost in front of an already-driven amp" use case: Tight 130 Hz (was 150 Hz), Drive 8 dB (was 12 dB), Tone 6000 Hz (was 5000 Hz).
- `docs/manual.md`: full user manual (what Overture is, where it sits in a chain, signal flow, complete parameter reference, usage tips).
- Editor controls for the new Bypass/Voicing/Oversampling parameters (toggle button + two combo boxes), so every automatable parameter has a working v0.1 control.
- Broadened Catch2 suite (23 → 51 test cases): clipper-voicing unit tests, tone-stack roll-off verification, oversampling-factor latency behaviour, bypass null-test and automation coverage, sample-rate sweep (44.1-192 kHz), mono/stereo/rejected bus-layout coverage, and long-run (several-second) NaN/Inf stability.

### Changed

- `docs/architecture.md` and `README.md` updated to describe the full v0.1.0 signal path (selectable Voicing, 4th-order Tone, Bypass, Oversampling) and parameter table.
