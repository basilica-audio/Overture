# Architecture

## Signal flow (v0.2.0)

```mermaid
flowchart LR
    IN[Input] --> HPF[Tight<br/>HPF, 20-400 Hz]
    HPF --> DRIVE[Drive<br/>0-40 dB]
    DRIVE --> UP[Nx Oversample<br/>2x/4x/8x]
    UP --> BITE[Bite shelf<br/>~700 Hz, INSIDE the<br/>drive-to-clipper path]
    BITE --> CLIP[Voicing clipper<br/>variable Asymmetry]
    CLIP --> KNEE[Knee Soften blend<br/>drive-dependent]
    KNEE --> DOWN[Nx Downsample]
    DOWN --> TILT[Bite Tilt<br/>+/-3 kHz shelf]
    TILT --> LEVEL[Level<br/>output trim]
    LEVEL --> MIX[Dry/Wet Mix]
    IN -.->|delay-compensated dry path| MIX
    MIX --> OUT[Output]
```

Everything from the Tight HPF through Level is the "wet" path, owned by `OvertureEngine` (`src/dsp/OvertureEngine.{h,cpp}`). The dry path is the untouched input signal, delayed to stay time-aligned with the wet path (see [Latency and oversampling](#latency-and-oversampling) below), then blended in at the Mix stage via `juce::dsp::DryWetMixer`. **Bypass** (`ParamIDs::bypass`) reuses this exact same path: `OvertureAudioProcessor::processBlock()` forces the *effective* Mix proportion fed to the engine to 0% while bypassed (regardless of the user's actual Mix setting), rather than skipping `OvertureEngine::process()` altogether - see [Bypass](#bypass) below.

This is the same v0.1 oversampling/latency/dry-wet architecture unchanged - v0.2.0 (`docs/design-brief.md`) only changes what happens *inside* Drive -> Clipper -> Tone, not the surrounding real-time-safety contract.

### Bite: frequency-dependent gain inside the drive-to-clipper path (new in v0.2.0)

`ParamIDs::biteAmount` (0-100%, default 65%) is **not** a pre-clip filter - it is a low-shelf (`biteShelf`, `juce::dsp::IIR`, corner fixed at 700 Hz) that runs *inside* the oversampled block, applied to the drive-scaled signal immediately before the voicing dispatch. It reduces the amplitude fed to the clipper below the shelf corner, scaled by `biteAmount` (up to 12 dB of cut at 100%), so bass is clipped *less* than treble - reproducing the reference circuit's own frequency-selective clipping mechanism (`docs/research-notes.md` SS3), not approximating its output with a separate filter ahead of the gain stage (v0.1's Tight control still does that job, unchanged, for the *pre-clip* tightening role).

At `biteAmount = 0`, the shelf is **skipped entirely** (no filter call, not merely given unity-gain coefficients) - this is what makes `bite_amount = 0` bit-identical to v0.1's flat-gain clipper input, the core regression guarantee `docs/design-brief.md` requires. `tests/EngineTests.cpp`'s frequency-dependent-gain proof feeds matched-level low/high-frequency sines through the clipper at a fixed Drive and confirms the measured clip-onset (peak) gap between them grows monotonically as `biteAmount` sweeps 0% -> 100%.

### Knee Soften: drive-dependent knee softening (new in v0.2.0)

`ParamIDs::kneeSoften` (0-100%, default 40%) blends each voicing's raw transfer-function output toward a rounded-corner variant (`src/dsp/KneeSoftening.h`: `raw + blend * (tanh(raw) - raw)`), where the blend amount is `(kneeSoften/100) * driveIntensity`, and `driveIntensity` is a real-time-safe, block-rate proxy for how hard the clipper is currently being driven (the last-commanded Drive dB value normalised against Drive's own 0-40 dB range). This reproduces the reference circuit's own knee-softening-cap behaviour (`docs/research-notes.md` SS3: "softens the corners... most noticeable when the drive control is maxed out") - the knee gets audibly softer as Drive increases, for a fixed `kneeSoften > 0`.

Applies uniformly to all three voicings, including Hard Clip, which has literally zero knee at `kneeSoften = 0` at any Drive level (a razor-sharp clamp) - `tanh()` rounding its otherwise-flat +/-1 ceiling is what makes it genuinely softenable. At `kneeSoften = 0`, the blend is always exactly 0 regardless of Drive (skipped, not called with a 0 blend), reproducing v0.1's Drive-invariant fixed-knee behaviour exactly.

### Asymmetry (new in v0.2.0)

`ParamIDs::asymmetryAmount` (0-100%, default 40%) exposes what was a fixed internal constant (`a = 0.2`) in v0.1's `AsymSoftClipper`, mapping linearly to the bias `a` in 0.0-0.5. Only the Asymmetric voicing consumes it (`ClipperVoicings::processSample`, `src/dsp/ClipperVoicing.h` - unchanged from v0.1, since it already took `asymmetry` as a parameter); Soft Symmetric and Hard Clip ignore it, exactly as in v0.1. The default 40% maps to `a = 0.2`, reproducing v0.1's fixed default exactly.

### Clipper voicing

The nonlinearity inside the oversampled block is selectable via the **Voicing** parameter (`ParamIDs::voicing`, `src/dsp/ClipperVoicing.h`) - unchanged from v0.1, and its enum indices remain frozen:

| Voicing | Transfer function | Character |
|---|---|---|
| Asymmetric (default) | `tanh(x + a) - tanh(a)`, `a` now variable via Asymmetry (`AsymSoftClipper`) | Single-ended/op-amp-style biased soft clip - the original v0.1 "808 boost" voicing. Both odd and even harmonics. |
| Soft Symmetric | `tanh(x)` (`SoftSymmetricClipper`) | Unbiased, odd-symmetric soft clip - smoother, push-pull/fuzz-adjacent saturation, odd harmonics only. |
| Hard Clip | `clamp(x, -1, 1)` (`HardClipper`) | No soft knee at `kneeSoften = 0` - the brightest/most aggressive voicing, more high-order odd harmonics; softenable via Knee Soften like the other two. |

`OvertureEngine::setClipperVoicing()` just stores the selected enum value (no allocation); `processChunk()` dispatches to the corresponding function once per (oversampled) sample via `ClipperVoicings::processSample()`, then conditionally blends through `KneeSoftening::apply()`. Voicing changes are **not** smoothed/crossfaded - a discrete mode switch causing an audible step at the switch instant is treated as expected, stompbox-toggle-like behaviour rather than a bug.

### Bite Tilt: post-clip bidirectional shelf (replaces v0.1's Tone LPF)

`ParamIDs::biteTilt` (-100%..+100%, default 0%) replaces v0.1's cut-only, 4th-order-Butterworth `tone` low-pass with a single 2nd-order RBJ-cookbook high-shelf (`biteTiltShelf`) anchored at a fixed ~3 kHz corner (reasoned proxy for the sourced ~3.2 kHz reference-circuit tone-tilt corner, `docs/research-notes.md` SS3). Negative values cut the shelf gain (darken, subsuming v0.1's entire cut-only range), positive values boost it (brighten) - a capability v0.1 entirely lacked. JUCE 8.0.14's `juce::dsp::IIR::(Array)Coefficients` has no true first-order shelf, so this uses the standard 2nd-order shelf with `Q = 1/sqrt(2)` (the same maximally-flat, non-resonant Q as the Tight HPF), the closest built-in equivalent to the brief's "roughly first-order/6 dB-oct" target.

At `biteTilt = 0`, the filter is **skipped entirely** (a true no-op, not a unity-gain filter call) - `tests/EngineTests.cpp`'s bidirectionality tests confirm flat is a genuine no-op, that negative/positive values darken/brighten monotonically, and that the fully-negative setting is at least as dark as v0.1's fully-closed Tone at a matched test frequency (measured directly against v0.1's own retired 4th-order cascade formula, not assumed).

**Migration:** a v0.1 saved session's `tone` value (1-8 kHz, cut-only) has no exact equivalent in the new bidirectional parameter - `OvertureAudioProcessor::setStateInformation()` detects a legacy `tone` PARAM node before `apvts.replaceState()` runs (a v0.1 XML tree has no `biteTilt` node at all for `replaceState()` to apply) and lossily maps it: 1000 Hz (v0.1's fully-closed/darkest Tone) -> -100% (maximally negative), 8000 Hz (fully-open/brightest) -> 0% (flat), linearly in between. This is a best-effort, not mathematically exact, equivalence - `docs/design-brief.md`'s "Migration" section documents this explicitly. Every other v0.2.0-only parameter falls back to its own default (not zero/garbage) on a legacy load.

### Bypass

`ParamIDs::bypass` (`juce::AudioParameterBool`) is returned from `OvertureAudioProcessor::getBypassParameter()`, which is JUCE's mechanism (JUCE 8.0.14, `juce::AudioProcessor::getBypassParameter()`) for AU/VST3/AAX/LV2 hosts to treat a plugin's own parameter as native bypass rather than calling the (here, unimplemented) `processBlockBypassed()`. `processBlock()` checks the parameter itself every block and forces the engine's effective Mix proportion to 0% while bypassed - the oversampler keeps running unchanged, so the plugin's reported latency (and therefore host plugin-delay-compensation) never changes on a bypass toggle. Because this goes through the same smoothed Mix path as a normal Mix automation move (`mixSmoothed` + `DryWetMixer`'s own internal ramp, see [Parameter smoothing](#parameter-smoothing)), engaging/disengaging Bypass mid-stream crossfades over roughly 100 ms rather than clicking - this is intentional (avoids a bypass-toggle pop), not a limitation; `tests/RobustnessTests.cpp`'s bypass null test accounts for it with an explicit warm-up period before measuring.

### Oversampling factor

`ParamIDs::oversampling` (`juce::AudioParameterChoice`: "2x"/"4x"/"8x", default 4x) selects `OvertureEngine`'s oversampling factor via `setOversamplingFactorPow2()`. Reconstructing the internal `juce::dsp::Oversampling` instance allocates, so `setOversamplingFactorPow2()` only ever *records* the requested factor - the actual (re)construction happens inside `OvertureEngine::prepare()`, called only from `OvertureAudioProcessor::prepareToPlay()` (never from `processBlock()`). Consequently, changing this parameter while audio is running takes effect the next time the host calls `prepareToPlay()` (transport stop/start, sample-rate change, reopening the plugin, etc.), not instantaneously - a deliberate trade-off to keep the audio thread allocation-free rather than building a message-thread-driven live-reconfiguration mechanism for a rarely-automated "quality" setting. `tests/LatencyTests.cpp` covers both the engine-level behaviour and this processor-level "change takes effect on next prepare" contract. The Bite shelf's own `ProcessSpec` (sample rate `sampleRate * oversampler->getOversamplingFactor()`) is derived from this factor inside the same `prepare()` call.

One JUCE 8.0.14-specific detail worth calling out: `juce::dsp::Oversampling` with `useIntegerLatency = true` (as `OvertureEngine` uses) rounds each cascaded 2x stage's fractional latency to the nearest integer sample *independently*, which can make an additional oversampling stage not increase the *reported* integer latency - empirically, at 48 kHz this engine measures 4 samples at 2x and 6 samples at both 4x and 8x. Latency is guaranteed non-decreasing as the factor increases, not strictly increasing at every step; `tests/LatencyTests.cpp` asserts the former, not the latter.

## Module map

| Directory | Responsibility |
|---|---|
| `src/dsp` | All audio-thread DSP: `AsymSoftClipper`/`ClipperVoicing` (the stateless nonlinearities and the `ClipperVoicing` enum/dispatch, unchanged from v0.1), `KneeSoftening` (new in v0.2.0 - the stateless knee-rounding blend function), `RealtimeCoefficients` (allocation-free biquad coefficient writes, `ovtr::applyBiquadCoefficients`), and `OvertureEngine` (the full signal chain). No allocation, locks, or I/O once `prepare()` has run. Independent of `juce::AudioProcessor` so it is directly unit-testable (see `tests/EngineTests.cpp`, `tests/AsymSoftClipperTests.cpp`, `tests/ClipperVoicingTests.cpp`, `tests/KneeSofteningTests.cpp`). |
| `src/params` | Parameter layout and `AudioProcessorValueTreeState` definitions - parameter IDs, ranges, defaults. Single source of truth for what a preset captures. |
| `src/presets` | M2 preset system (`.scaffold/specs/preset-system-m2.md`): `PresetManager` (factory/user preset discovery, load/save/import/export, dirty tracking, default resolution) and `PresetBar` (the editor strip), plus `Localisation` (the M2 i18n frame, `resources/i18n/de.txt`). Copied verbatim from the Nave pilot implementation - see `docs/design-brief.md`. |
| `src/PluginProcessor.*` | Host plumbing: APVTS construction, `prepareToPlay`/`processBlock`/`reset`, latency reporting, state save/load (including the v0.1 `tone` -> v0.2.0 `biteTilt` migration), `getBypassParameter()`, `PresetManager` construction/wiring. Reads APVTS values and pushes them into `OvertureEngine` every block; does not implement any DSP itself. |
| `src/PluginEditor.*` | A simple, functional v0.1/v0.2 GUI: one rotary slider per continuous parameter (`SliderAttachment`), a toggle for Bypass (`ButtonAttachment`), combo boxes for the discrete Voicing/Oversampling choices (`ComboBoxAttachment`), and the `PresetBar` strip docked at the top. Every automatable parameter has a working control; a custom vector-drawn GUI is a later milestone (M3). |

Dependency direction is one-way: `PluginEditor` -> `params`/`presets` (via attachments) and `PluginProcessor` -> `params` + `dsp` + `presets`. `src/dsp` has no upward dependency on the processor or UI, which is what keeps `OvertureEngine` testable in isolation.

## Latency and oversampling

The clipper (and, as of v0.2.0, the Bite shelf ahead of it) runs inside an oversampled block (`juce::dsp::Oversampling<float>`, half-band polyphase IIR, `useIntegerLatency = true`, factor selectable 2x/4x/8x - see [Oversampling factor](#oversampling-factor) above) so that the clipper's high-frequency harmonics are generated and filtered above the host sample rate before being downsampled back, keeping aliasing out of the audible band. This oversampling is the only source of the plugin's reported latency: `OvertureEngine::getLatencySamples()` returns `oversampler.getLatencyInSamples()` (an exact integer, since `useIntegerLatency` is enabled), and `OvertureAudioProcessor::prepareToPlay()` reports it to the host via `setLatencySamples()`, so host-side plugin delay compensation (PDC) accounts for the whole chain. Bite Tilt and Level run post-downsample, at the base sample rate, and are not part of the reported latency.

The dry path used by the Mix control has to stay time-aligned with this delayed wet path. Rather than a hand-rolled delay line, `OvertureEngine` uses `juce::dsp::DryWetMixer`: the pre-processing signal is captured via `pushDrySamples()` before any wet-path filtering touches the buffer, and `setWetLatency(getLatencySamples())` configures the mixer's internal delay line to match. `mixWetSamples()` then blends the two back together, so at Mix = 0% the output is a sample-accurate passthrough of the input, once shifted by `getLatencySamples()` (this is exactly what `tests/EngineTests.cpp`'s null test verifies, to < -90 dBFS residual, now with every v0.2.0 control also set to a non-neutral value).

One JUCE 8.0.14 behaviour worth calling out because it cost real debugging time (see `tests/DryWetMixerContractTests.cpp`): `DryWetMixer`'s internal dry/wet gain smoothers default their *target* to fully wet (`mix == 1.0`) until `setWetMixProportion()` is called, and the mixer's own `reset()` (invoked from its `prepare()`) only snaps the smoothers' *current* value to whatever *target* is set at that moment - it has no idea what the "real" starting Mix parameter value should be. Skipping this would mean a freshly prepared engine audibly ramps in from 100% wet over the mixer's internal ~50ms default ramp on every `prepareToPlay()`, regardless of the actual Mix parameter. `OvertureEngine::prepare()` works around this by calling `dryWetMixer.setWetMixProportion(lastMixProportion)` *before* its own `reset()` runs, so the mixer is already sitting at the correct dry/wet balance for the very first `process()` call.

The Tight high-pass, Bite shelf, and Bite Tilt shelf are all plain IIR filters (`juce::dsp::IIR::Coefficients::makeHighPass`/`makeLowShelf`/`makeHighShelf`, primed once in `prepare()`, then re-derived every processed chunk via the allocation-free `juce::dsp::IIR::ArrayCoefficients` + `ovtr::applyBiquadCoefficients` path - see `src/dsp/RealtimeCoefficients.h`); none is part of the reported latency - like any IIR filter, each has its own small, frequency-dependent group delay (a few samples at most, well outside its passband), treated as ordinary filter character rather than something to compensate. `tests/EngineTests.cpp`'s near-linearity test accounts for this by searching a small (+/-8 sample) alignment window rather than assuming an exact match at the oversampling latency alone.

`OvertureEngine::process()` also defensively chunks any block larger than the size declared to `prepare()` (`preparedMaxBlockSize`) into `processChunk()` calls of at most that size, since the oversampler's and `DryWetMixer`'s internal buffers are both fixed to that size and only guard the invariant with a `jassert` (compiled out in Release builds) - `tests/EngineTests.cpp` verifies this produces bit-identical output to what a well-behaved, correctly-chunking host would get.

## Parameter smoothing

- **Drive** and **Level** are plain gain stages (`juce::dsp::Gain<float>`), which ramp sample-accurately via their own internal `SmoothedValue` (`setRampDurationSeconds`).
- **Tight**, **Bite**, **Knee Soften**, **Asymmetry**, and **Bite Tilt** all recompute filter coefficients or blend factors once per processed chunk rather than per sample - recomputing IIR coefficients involves trig calls, so this is not cheap to interpolate directly; each is smoothed with a `juce::SmoothedValue` (`Multiplicative` for the frequency-perceived-logarithmically Tight; `Linear` for the rest, all percentages/proportions) and re-applied once per chunk - a standard real-time-safe compromise. Knee Soften's own drive-dependence uses the last-commanded Drive dB value directly (not itself smoothed - `juce::dsp::Gain` doesn't expose a "current ramped value" getter), the same block-rate-snapshot compromise the coefficient recomputation already makes.
- **Mix** is smoothed both by the engine's own `juce::SmoothedValue<float, ValueSmoothingTypes::Linear>` (feeding `DryWetMixer::setWetMixProportion()` once per chunk) and by `DryWetMixer`'s own internal ~50ms ramp on top of that. **Bypass** reuses this exact path (see [Bypass](#bypass) above), so it inherits the same smoothing.
- **Voicing** and **Oversampling** are discrete choices, not smoothed: Voicing switches the clipper nonlinearity instantly (an audible step at the switch instant is expected, like a stompbox toggle); Oversampling only takes effect on the next `prepare()` call (see [Oversampling factor](#oversampling-factor) above), so there is nothing to smooth mid-stream.
- All smoothers are seeded to their real starting value in `OvertureEngine::prepare()` (see `lastTightHz`/`lastBiteAmountPercent`/`lastKneeSoftenPercent`/`lastAsymmetryAmountPercent`/`lastBiteTiltPercent`/`lastMixProportion`), so re-preparing (sample-rate change, etc.) never resets a live parameter back to a built-in default or lets a smoother ramp from an invalid 0 Hz/0.0 starting point.

## Real-time safety

- `OvertureAudioProcessor::processBlock()` starts with `juce::ScopedNoDenormals`.
- All DSP state (filters, the oversampler, the Bite/Bite Tilt shelves, the dry/wet delay line) is allocated in `prepare()`/`prepareToPlay()` and never reallocated on the audio thread.
- `reset()` clears all filter/oversampler/delay-line state without deallocating (`OvertureEngine::reset()`, called from both `AudioProcessor::reset()` and internally from `prepare()`).
- Parameter values are read via `apvts.getRawParameterValue()` atomics in `processBlock()`, never via `apvts.getParameter()->getValue()` (which is not guaranteed lock/allocation-free) and never via `String`-keyed lookups on the audio thread.
- `OvertureEngine::process()` treats a zero-sample block as a safe no-op before touching any filter/oversampler state, and defensively chunks oversized blocks (see [Latency and oversampling](#latency-and-oversampling) above).
- Filter cutoff frequencies passed to coefficient-generation calls are clamped below Nyquist (`clampBelowNyquist`, in `OvertureEngine.cpp`) as defensive insurance against invalid coefficients if the plugin is ever prepared at an unusually low sample rate.
- Every per-chunk coefficient recompute (Tight HPF, Bite shelf, Bite Tilt shelf) uses `juce::dsp::IIR::ArrayCoefficients` (stack-computed) + `ovtr::applyBiquadCoefficients` (`src/dsp/RealtimeCoefficients.h`) rather than the allocating `IIR::Coefficients::make*()` calls (which `new` a fresh ref-counted object per call) - a permanent regression guard (`tests/AllocationTests.cpp`, globally instrumenting `operator new`/`delete`) fails the build if `processBlock()`/`OvertureEngine::process()` ever allocates while any control is being automated, extended in v0.2.0 to the Bite/Knee Soften/Asymmetry/Bite Tilt controls specifically.
- The Bite shelf and Bite Tilt shelf are **skipped entirely** (not called with unity-gain/zero-blend coefficients) when their controlling parameter is at its neutral value (`biteAmount = 0`, `biteTilt = 0`) or, for Knee Soften, when the computed blend is 0 - this is both a real-time-safety-neutral micro-optimisation and the mechanism that makes the "0% = v0.1-identical"/"flat = true no-op" backward-compatibility guarantees bit-exact rather than approximate.
- `OvertureEngine::setOversamplingFactorPow2()` never reallocates - it only records the requested factor as a plain `int` member; the actual (re)construction of `juce::dsp::Oversampling` happens exclusively inside `prepare()`, which is only ever called from `prepareToPlay()` (never `processBlock()`).
- `OvertureAudioProcessor::getBypassParameter()` implements bypass via the same allocation-free, atomics-driven Mix path as every other parameter (`bypassFlag->load()` in `processBlock()`), not via a separate code path or `processBlockBypassed()`.
- `PresetManager` (`src/presets/PresetManager.{h,cpp}`) never touches the audio thread - every public method does file I/O, JSON parsing, or `juce::String`/`juce::var` allocation, called only from the message thread (constructor, `PresetBar` UI callbacks). Its one audio-thread-adjacent code path, the `AudioProcessorValueTreeState::Listener::parameterChanged()` dirty-flag callback, is a single lock-free `std::atomic<bool>` store and nothing else.
