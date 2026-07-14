# Overture — tight boost / overdrive (guitar)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Metal up your ass** symphonic-metal plugin suite (`github.com/metal-up-your-ass`).

## What this is
Overture is a TS-808-style **tight boost / overdrive** for metal guitar — the pre-amp tightening stage run in front of a high-gain amp. It strips low end before the clipper (the "808 boost" trick) so palm mutes stay tight, then drives an oversampled asymmetric soft clipper for overdrive character. AU / VST3 / Standalone.

## Status (v0.1 — bootstrap complete)
Core DSP working, **23/23 Catch2 tests green**, CI (macOS + Windows, pluginval strictness 10 + auval) green. GUI is a functional v0.1 slider editor (custom LookAndFeel is roadmap M3). No signing yet (roadmap M4). See GitHub **milestones/issues** for the open work.

## DSP
Signal: `input → Tight HPF (2nd-order Butterworth, 20–400 Hz) → Drive → 4× oversampled asymmetric tanh clipper → Tone LPF (1–8 kHz) → Level → juce::dsp::DryWetMixer`.
- Engine: `src/dsp/OvertureEngine.{h,cpp}` (processor-independent, unit-testable) + `src/dsp/AsymSoftClipper.h` (stateless `y = tanh(x+a) − tanh(a)`, a=0.2).
- Params (APVTS, `src/params/`): `tight`, `drive`, `tone`, `level`, `mix`.
- Latency = oversampler latency, reported via `setLatencySamples`; dry path delay-compensated by `DryWetMixer::setWetLatency`.

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
