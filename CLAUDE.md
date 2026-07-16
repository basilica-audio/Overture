# Overture — tight boost / overdrive (guitar)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Overture is a TS-808-style **tight boost / overdrive** for metal guitar — the pre-amp tightening stage run in front of a high-gain amp. It strips low end before the clipper (the "808 boost" trick) so palm mutes stay tight, then drives an oversampled, selectable-voicing soft/hard clipper for overdrive character. AU / VST3 / Standalone.

## Status (v0.2.0 — deep-dive DSP rework, M2 presets/i18n done)
Core DSP reworked for v0.2.0 (research-driven, `docs/design-brief.md` + `docs/research-notes.md`), M2 preset system + German i18n frame shipped, **96/96 Catch2 tests green** locally. GUI is a functional v0.1/v0.2 slider/toggle/combo-box editor + PresetBar covering every parameter (custom LookAndFeel is roadmap M3). No signing yet for the plugin itself (roadmap M4) - releases build/sign via `.github/workflows/release.yml`. See GitHub **milestones/issues** for the open work.

## DSP
Signal: `input → Tight HPF (2nd-order Butterworth, 20–400 Hz) → Drive → Nx oversampled (2x/4x/8x) [Bite shelf (~700 Hz, inside the drive-to-clipper path) → selectable clipper (Voicing, variable Asymmetry) → Knee Soften blend] → Bite Tilt (+/-3 kHz shelf) → Level → juce::dsp::DryWetMixer`. Bypass reuses the Mix path (forces effective mix to 0%, crossfaded) rather than skipping processing, so reported latency never changes on a bypass toggle.
- Engine: `src/dsp/OvertureEngine.{h,cpp}` (processor-independent, unit-testable) + `src/dsp/AsymSoftClipper.h` + `src/dsp/ClipperVoicing.h` (three stateless clipper voicings + dispatch enum) + `src/dsp/KneeSoftening.h` (new, stateless knee-rounding blend) + `src/dsp/RealtimeCoefficients.h` (allocation-free biquad coefficient writes).
- Params (APVTS, `src/params/`): `tight` (100 Hz default), `drive` (3 dB default), `biteAmount` (65% default), `kneeSoften` (40% default), `asymmetryAmount` (40% default), `biteTilt` (0% default, replaces the retired `tone`), `level`, `mix`, `bypass`, `voicing`, `oversampling`. Defaults tuned for the best-documented "near-zero clipper drive, Level does the pushing" use case, not a standalone distortion.
- Latency = oversampler latency, reported via `setLatencySamples`; dry path (and Bypass) delay-compensated by `DryWetMixer::setWetLatency`. Oversampling-factor changes only take effect on the next `prepareToPlay()` (reconstructing the oversampler allocates, so it never happens on the audio thread) - see `docs/architecture.md`.
- State migration: a v0.1 session's `tone` value is lossily mapped onto `biteTilt` on load (`OvertureAudioProcessor::setStateInformation()`) - see `docs/design-brief.md`'s "Migration" section.
- M2 preset system (`src/presets/`, copied verbatim from the Nave pilot): `PresetManager`/`PresetBar`/`Localisation`. Nine factory presets (`presets/factory/*.json`, documented in `docs/presets.md`). German frame translation (`resources/i18n/de.txt`) - core/DSP parameter names are never translated.
- User-facing docs: `docs/manual.md` (full parameter reference + tips), `docs/presets.md` (factory preset intents).

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
Tracked as GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) and issues. Read them with `gh issue list` / `gh api repos/basilica-audio/overture/milestones`.

## Suite context
Style references: sibling `basilica-audio/Crypta` (bass) and the other suite plugins (tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis). Shared scaffold conventions come from the same template.
