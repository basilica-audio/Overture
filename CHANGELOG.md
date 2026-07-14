# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
