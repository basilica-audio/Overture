# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **Plugin metadata now carries the vendor URL, the copyright string, a real description and
  the VST3 sub-category.** `COMPANY_WEBSITE`, `COMPANY_COPYRIGHT` and `DESCRIPTION` were never
  set, so a shipped bundle carried an empty `NSHumanReadableCopyright`, an empty VST3 vendor
  URL, and an AU `description` that was just the plugin name again; `VST3_CATEGORIES` fell back
  to JUCE's bare `Fx` default, which filed every plugin in the suite under the same
  undifferentiated heading in a VST3 host's browser. Overture now declares
  `Fx Distortion` (JUCE 8.0.14, `juce_add_plugin`). **Plugin identity is unchanged** — the VST3 class
  ID is derived from `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone
  (`juce_VST3ModuleInfo.h`'s `VST3Interface::jucePluginId`) and the AU type/subtype/manufacturer
  triple is untouched, so existing sessions keep resolving to the same plugin.

### Fixed

- **The README no longer tells users the binaries do not exist.** The Installation section
  said *"No pre-built binaries are published yet"* while the banner four lines above it linked
  the Releases page, and the banner in turn described the macOS builds as *"currently
  unsigned"*. Both claims were false. The Installation section now describes the actual
  download-and-copy flow, and the banner states what the release workflow actually produces:
  verified against the shipped `v0.5.0` `.component` with `codesign --verify --strict`
  (`Developer ID Application: Yves Vogl (M5WT732AY5)`), `spctl -a -t open`
  (`source=Notarized Developer ID`) and `stapler validate`.
- **The documented factory-preset count matches what ships** (nine -> eleven); `presets/factory/` holds 11.

### Added

- **A `Documentation` section in the README** pointing at the user manual, the factory-preset
  reference, the changelog and the product page — the manual was only reachable from a
  sentence in the middle of the Signal flow section.

## [0.5.0] - 2026-08-20

An accessibility release. The photoreal faceplate's eight brass knobs are now reachable and operable from the keyboard alone, and the Bypass lever finally identifies itself to screen readers as the toggle it is. Nothing in the audio path changed - a v0.4.0 session loads and renders bit-identically.

### Added

- **WAI-ARIA-style keyboard stepping on the eight knobs** (`src/gui/KeyboardSteps.h`, PR #29). Arrow moves 1% of the control's range, Shift+Arrow 0.1% - the keyboard analog of the Shift-drag fine mode the knobs have always had on the mouse - PageUp/PageDown 10%, and Home/End the range extremes. Steps are taken in the slider's proportional domain, so a skewed range sweeps as evenly under the arrow keys as under a drag, and the result is still snapped to the parameter's own interval grid, so quantisation is never violated. A focus flag alone would not have been enough: JUCE's stock handler steps by the raw parameter interval (0.01 dB across Drive's 40 dB range - 4000 presses end to end) and bails out the moment any modifier key is held, so Shift+Arrow did nothing whatsoever. Ctrl/Cmd-modified arrows are deliberately passed through to the host as shortcuts.
- New `tests/gui/EditorAccessibilityTests.cpp` coverage pinning the contract: focus reachability of all eight knobs, the Bypass toggle, both choice combo boxes and the scale button (asserted by count, so a zero-match loop cannot pass vacuously); coarse/fine/page/Home/End stepping and Ctrl/Cmd passthrough on Drive; and the toggle's reported accessibility role.

### Fixed

- **The eight knobs could not be reached by keyboard at all** (PR #29). `juce::Slider::init()` ships `setWantsKeyboardFocus(false)` (JUCE 8.0.14, `juce_Slider.cpp:1461`) and `FilmstripKnob` never opted back in, so Tab skipped straight past every knob, the focus ring already drawn in `paint()` could never appear, and no key press ever arrived. They now take focus in reading order - header/scale, preset bar, then the input, clipper, output and utility bays - and show their ring while focused. This is the gap behind v0.4.0's "keyboard operation works unchanged" accessibility note, which was true of the stock JUCE classes in the abstract but not of the controls as they actually shipped.
- **Bypass was announced as a plain button rather than a toggle** (PR #29). `FilmstripToggle` derived from `juce::Button`, which reports `AccessibilityRole::button`; the checkable/checked *state* was correct, but the control was mis-filed in VoiceOver's rotor and NVDA's by-type quick navigation, so a user browsing by control type would not find Bypass among the toggles. It now derives from `juce::ToggleButton` and reports `AccessibilityRole::toggleButton`. The lever's custom artwork is unaffected - `paintButton()` is a full override, so no default drawing leaks through.

### Known limitations

- This release covers **keyboard** operation (WCAG 2.1.1, 2.4.7). Assistive-technology increment and decrement actions - VoiceOver's rotor, NVDA's value adjustment - never reach `keyPressed()`; they go through JUCE's accessibility value interface, which still reports the raw parameter interval as its step size. A screen-reader user therefore still moves Drive 0.01 dB per action. Closing that gap means giving each control a custom `AccessibilityHandler` carrying its own value interface, which is the next step and is not part of this release.

## [0.4.0] - 2026-08-19

The M3 GUI release: the functional slider/toggle/combo-box editor is replaced by the
photoreal skeuomorphic faceplate editor, following `basilica-audio/silentium`'s M3
pilot pattern (PR #23, merged after the pilot sign-off that gated it).

### Added

- **Photoreal skeuomorphic GUI (M3)** - replaces the v0.1/v0.2 functional slider/toggle/combo-box editor with a custom editor built from pre-rendered Blender assets (the suite's gui-pipeline renders, copied into `resources/gui/` and embedded via BinaryData so the repo stays self-contained), replicating `basilica-audio/silentium`'s M3 pilot pattern: a stone/gunmetal faceplate with four engraved section bays (input, clipper, output, utility - see `.scaffold/gui-assets/faceplate-overture-v1/layout-manifest.json`), brass filmstrip knobs (128 frames, -135deg..+135deg) for the 8 continuous parameters, and a brass lever toggle for `bypass`. See `docs/gui-preview.png` for the rendered result.
- **Suite-reusable GUI component family** (`src/gui/`), ported verbatim from Silentium's M3 pilot (Silentium-agnostic by design): `FilmstripKnob` (filmstrip-backed `juce::Slider`, Shift = fine drag, double-click resets to the parameter default, mouse-wheel support), `FilmstripToggle` (4-frame `juce::Button`), `BasilicaLookAndFeel` (gold serif labels with an engraved dual-shadow look and a WCAG-AA-verified opaque backing chip - the interim JUCE-drawn label solution until per-control text is baked into the faceplate art), and `ImageDensity.h` (@1x/@2x asset tier selection). `AnalogMeter` was intentionally NOT ported - Overture's layout manifest declares no meter bay (it's a boost/overdrive, not a gate/compressor with a gain-reduction reading to show).
- **`voicing`/`oversampling` choice controls**: no dedicated filmstrip/combo-box art ships in this asset wave (the manifest declares 2 choice slots, not per-control art), so these two discrete parameters remain stock `juce::ComboBox` instances, explicitly recoloured (background/text/outline/arrow) to the same gold-on-gunmetal palette `BasilicaLookAndFeel`'s labels use, so they read as part of the same engraved family rather than a visibly foreign JUCE default.
- **Stepped window scaling** (100/150/200%, via a control next to the preset bar) - no free resize, because the artwork is pre-rendered at fixed density tiers. The chosen step persists in the plugin state (a plain `uiScaleStep` property on the APVTS tree) and round-trips through host session save/reload.
- **Accessibility**: all controls derive from stock `juce::Slider`/`juce::Button`/`juce::ComboBox`, so JUCE's accessibility handlers, keyboard operation, and host parameter attachments work unchanged; accessible titles are set from parameter names (with declared units - dB/Hz/% - included in the reported value string), and creation order matches the visual reading order (header/scale, preset bar, input bay, clipper bay, output bay, utility bay) for focus traversal.
- New GUI test suite (ported and adapted from Silentium's M3 pilot): filmstrip frame-math edges, label text/backing-chip WCAG 1.4.3 contrast ratio, layout invariants asserted against the manifest's own bay rects, knob/toggle/combo-box accessible name and value tests, editor construct/destroy, and an offscreen editor snapshot (written to `build/gui-preview.png`, committed as `docs/gui-preview.png`) verified non-blank.
- Elision-safe allocation-guard self-test and a sample-rate-matrix reprepare test
  (44.1k -> 96k -> 192k, crossing 32/2048-sample blocks and a mono/stereo bus-layout
  change) (PR #27).

### Changed

- `docs/manual.md`: a "Reported latency" table (4 samples at 2x, 6 samples at 4x and
  8x, with the rounding reason the 4x -> 8x step can land on the same total), gate
  ballistics a user can act on (stereo-linked detection, sub-millisecond opening,
  90 dB range instead of a hard mute), and a corrected stale claim - the Feedback
  voicing does consume Asymmetry, by morphing the diode law itself (PR #25).
- Branding: v3 flat squircle icon (no dish/ring) (PR #26).

### Compatibility

- **The five v0.3.0 engine parameters (`gate`, `gateThreshold`, `gateRelease`, `clipQuality`, `kneeResponse`) still ship without dedicated photoreal controls in this asset wave** - the faceplate manifest predates v0.3.0 and declares no bays or choice slots for them, so v0.3.0's "Photoreal controls arrive with the M3 GUI" expectation moves to the next asset wave. They remain fully host-automatable and reachable through the host's generic parameter view, exactly as in v0.3.0.

## [0.3.0] - 2026-07-27

The release that replaces the last "textbook static waveshaper" excuse with a genuinely circuit-solved feedback clipper, and adds the built-in gate the plugin's own stated use case - palm-muted, low-tuned chugging - was never credible without. Every new parameter defaults to a neutral value, so **a v0.2.0 session or preset loads and sounds bit-identical** (asserted against goldens recorded from the v0.2.0 engine, not argued).

### Added

- **Built-in noise gate** (`gate`, `gateThreshold`, `gateRelease` - `src/dsp/NoiseGate.h`). A log-domain, per-sample gate rather than a block-rate one: a 5 ms mean-square detector behind a fixed 100 Hz/5 kHz sidechain pair, a non-linear-capacitor anti-chatter smoother whose time constant collapses from 30 ms to 2 ms as the level step grows, 4 dB of hysteresis, a 20 ms retriggering hold, and a **dB-linear** closing ramp (the VCA/THAT-DN100 signature - it sounds like a fader being pulled, not a tail being squashed). The detector taps the plugin input, before Tight and before Drive; the gain is applied to the wet path input, so gated noise never reaches the clipper. The dry path (Mix < 100%) is deliberately ungated. **Zero added latency** - no lookahead in v0.3.0.
  - `gateRelease = Auto` (the default) runs a dual-envelope, program-dependent release: a staccato mute closes the gate at 1000 dB/s while a ringing chord is released at its own measured decay rate plus a small margin, so one setting covers both. `Fast` (800 dB/s) and `Slow` (60 dB/s) are the fixed escape hatches.
- **"Feedback" clipper voicing** (a fourth `voicing` entry - `src/dsp/FeedbackClipperStage.h`). A trapezoidally-discretised, safeguarded-Newton solve of the op-amp/anti-parallel-diode/RC feedback loop, not a transfer curve: the 51 pF feedback capacitor puts a drive-dependent lowpass pole (~61 kHz at Drive 0, ~5.7 kHz at Drive max) *inside* the nonlinearity, so the stage's gain, its knee and its high-frequency rounding all move together with the signal and the Drive control. Drive maps to the feedback resistance R2 = 51 kΩ + D·500 kΩ rather than to a pre-clipper gain; Asymmetry morphs the diode law itself toward the two-series/one-reverse variant. Bite and Knee Soften have no effect in this voicing (the circuit's own 720 Hz pre-emphasis *is* the bite mechanism, and its knee is physical), and the DC blocker is always active.
  - **This voicing is a touch-sensitive, programme-level clipper by design.** With 0 dBFS calibrated to 2 Vpk and ~21.5 dB of minimum in-band loop gain into a silicon diode knee, its linear region at Drive 0 ends around −40…−35 dBFS - a −12 dBFS programme clips it hard, exactly as the reference circuit does. It is not a clean-at-unity boost.
- **"Enhanced" clip quality** (`clipQuality` - `src/dsp/AdaaWaveshaper.h`, `src/dsp/DcBlocker.h`). First-order antiderivative anti-aliasing around the three memoryless voicings, plus a 5 Hz DC blocker on the clipper output. Measured on a bin-centred 1245 Hz tone into Hard Clip at 0 dBFS/Drive 40: alias energy drops from −23.8 to −42.5 dBFS at 2× oversampling, and from −33.2 to −57.0 dBFS at the default 4×. The harmonic spectrum is unchanged within 0.3 dB. `Classic` (the default) remains the exact v0.2.0 path, bit for bit.
- **"Signal" knee response** (`kneeResponse`). Knee Soften's intensity can now come from an instant-attack/30 ms-release peak envelope on the oversampled clipper input instead of the open-loop `Drive/40` proxy, so a quiet passage at Drive 40 gets a hard knee and a slammed input gets the soft one. `Drive` (the default) is the bit-identical v0.2.0 behaviour.
- **Two factory presets**: **Tight Rhythm Gate** (gate on at −45 dB, Auto release, Signal knee, Enhanced) and **Circuit Drive** (the Feedback voicing at Drive 12 dB). The nine existing presets are byte-frozen and load with every new parameter at its neutral default.
- **State-schema stamping**: saved state now carries a `stateSchema="3"` attribute. No value rewriting happens on load - the new parameters' defaults *are* the v0.2.0-equivalent values - so the attribute exists purely so a future migration can branch on an explicit version instead of sniffing for individual parameters.
- Substantial new test coverage, all of it measurement-based: `tests/NoiseGateTests.cpp` (T-G1–T-G7), `tests/FeedbackClipperTests.cpp` (T-F1–T-F5), `tests/AdaaTests.cpp` (T-A1–T-A3), plus v0.2.0 bit-identity goldens, state-migration, sub-block-zipper and allocation-guard cases in the existing suites.

### Changed

- **Parameter and coefficient updates moved from block rate to 32-sample sub-blocks.** The Tight HPF, Bite shelf and Bite Tilt shelf used to recompute their coefficients exactly once per host block (~86 Hz at 512 samples/48 kHz), which is audible as zipper noise when Bite Tilt or Tight is automated. The update cadence is now ~1.5 kHz, and a coefficient is only re-derived when its smoothed value has actually moved - which is what keeps a settled parameter's output bit-identical to v0.2.0 despite the 16× higher cadence.

### Compatibility

- **The `voicing` parameter gained a fourth entry, which shifts pre-existing *normalised automation lanes*.** Saved state and presets persist the choice *index*, so sessions and presets load correctly and are bit-identical (asserted in `tests/StateTests.cpp`). A host automation lane, however, records normalised values: a lane holding 1.0 used to select "Hard Clip" (2 of 2) and now selects "Feedback" (3 of 3). Voicing is documented and coded as a discrete configuration control rather than a performance control; if you have automated it, re-check those lanes. The alternative - a second, separate voicing menu - would have permanently damaged the product.
- **v0.3.0 ships these five parameters without dedicated on-screen controls.** They are fully host-automatable and reachable through your host's generic parameter view. Photoreal controls arrive with the M3 GUI. The only editor-visible change in this release is the Voicing menu's new fourth entry.
- Reported latency is unchanged at every oversampling factor. The gate adds zero; ADAA's half-sample delay is at the *oversampled* rate and is deliberately not reported.

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
