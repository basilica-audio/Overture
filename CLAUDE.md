# Overture — tight boost / overdrive (guitar)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Metal up your ass** symphonic-metal plugin suite (`github.com/metal-up-your-ass`).

## What this is
Overture is a TS-808-style **tight boost / overdrive** for metal guitar — the pre-amp tightening stage run in front of a high-gain amp. It strips low end before the clipper (the "808 boost" trick) so palm mutes stay tight, then drives an oversampled, selectable-voicing soft/hard clipper for overdrive character. AU / VST3 / Standalone.

## Status (v0.1.0 — M1 DSP completion & test coverage done)
Core DSP complete for v0.1.0, **51/51 Catch2 tests green** locally. GUI is a functional v0.1 slider/toggle/combo-box editor covering every parameter (custom LookAndFeel is roadmap M3). No signing yet (roadmap M4). See GitHub **milestones/issues** for the open work.

## DSP
Signal: `input → Tight HPF (2nd-order Butterworth, 20–400 Hz) → Drive → Nx oversampled (2x/4x/8x) selectable clipper (Voicing) → Tone LPF (4th-order Butterworth cascade, 1–8 kHz) → Level → juce::dsp::DryWetMixer`. Bypass reuses the Mix path (forces effective mix to 0%, crossfaded) rather than skipping processing, so reported latency never changes on a bypass toggle.
- Engine: `src/dsp/OvertureEngine.{h,cpp}` (processor-independent, unit-testable) + `src/dsp/AsymSoftClipper.h` + `src/dsp/ClipperVoicing.h` (three stateless clipper voicings + dispatch enum).
- Params (APVTS, `src/params/`): `tight` (130 Hz default), `drive` (8 dB default), `tone` (6000 Hz default), `level`, `mix`, `bypass`, `voicing`, `oversampling`. Defaults tuned for a boost-in-front-of-a-driven-amp use case, not a standalone distortion.
- Latency = oversampler latency, reported via `setLatencySamples`; dry path (and Bypass) delay-compensated by `DryWetMixer::setWetLatency`. Oversampling-factor changes only take effect on the next `prepareToPlay()` (reconstructing the oversampler allocates, so it never happens on the audio thread) - see `docs/architecture.md`.
- User-facing docs: `docs/manual.md` (full parameter reference + tips).

## Build & test
```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"      # shared JUCE 8.0.14 + Catch2 cache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Tests Overture_Standalone --parallel 4
ctest --test-dir build --output-on-failure
```
Release/universal + pluginval + auval run in CI, not locally.

## Conventions & guardrails
- JUCE 8.0.14 via CPM · C++20 · AGPLv3 · Pamplejuce `SharedCode` pattern · manufacturer `Yvsv`, plugin code `Ovtr`, `com.yvesvogl.overture`.
- **Real-time safety:** no alloc/lock/file-IO/logging on the audio thread; allocate in `prepareToPlay`; `reset()` clears all state; `ScopedNoDenormals`; smoothed params.
- **DryWetMixer gotcha (JUCE 8.0.14):** prime `setWetMixProportion(mix)` **before** `reset()` in `prepare()`, else it ramps in from 100 % wet on every `prepareToPlay` (regression-tested in `tests/DryWetMixerContractTests.cpp`).
- **Branch:** `main` is protected — no direct commits; use a feature branch + PR, green CI required (Conventional Commits). New DSP needs tests (null/reference, NaN/Inf sweep, state round-trip, latency).

## Roadmap
Tracked as GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) and issues. Read them with `gh issue list` / `gh api repos/metal-up-your-ass/overture/milestones`.

## Suite context
Style references: sibling `metal-up-your-ass/twist-your-guts` (bass) and the other suite plugins (tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis). Shared scaffold conventions come from the same template.
