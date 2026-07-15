# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Audio-thread heap allocation in `OvertureEngine::process()` (#12):** the Tight HPF and both Tone LPF stages recomputed their `juce::dsp::IIR::Coefficients` every block via `Coefficients::makeHighPass`/`makeLowPass`, each of which heap-allocates a new ref-counted object (up to 6 allocations/6 deallocations per `processBlock()` call). Replaced with `juce::dsp::IIR::ArrayCoefficients` (stack-computed) written directly into the already-allocated `Coefficients` storage via a new `src/dsp/RealtimeCoefficients.h` helper - the same non-allocating pattern already used by sibling plugins Lancet and Twist Your Guts. Regression-tested by `tests/AllocationTests.cpp`, which globally instruments `operator new`/`delete` and fails if `processBlock()`/`OvertureEngine::process()` ever allocates again.
- **No defensive clamp against oversized blocks in `processBlock()`/`OvertureEngine::process()` (#13):** a host handing `process()` more samples than the `spec.maximumBlockSize` declared to `prepare()` would overrun the oversampler's and `DryWetMixer`'s internal buffers, which only guard that invariant with a `jassert` (compiled out in Release builds). `OvertureEngine::process()` now defensively splits any oversized block into chunks of at most the prepared maximum before processing each chunk, matching the real, Release-safe chunking guard already used by sibling plugins Twist Your Guts and Miserere. Regression-tested by a new `tests/EngineTests.cpp` case that feeds a 64x-oversized block through `process()` and verifies it produces bit-identical output to a correctly host-chunked reference run.

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
