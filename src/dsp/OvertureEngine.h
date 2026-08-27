#pragma once

#include <juce_dsp/juce_dsp.h>

#include "AdaaWaveshaper.h"
#include "AsymSoftClipper.h"
#include "ClipperVoicing.h"
#include "DcBlocker.h"
#include "EnvelopeFollower.h"
#include "FeedbackClipperStage.h"
#include "NoiseGate.h"

// How Knee Soften's intensity factor is derived (ParamIDs::kneeResponse).
// Values are persisted as an AudioParameterChoice index - append only.
enum class KneeResponseMode
{
    // v0.2.0 behaviour: the open-loop lastDriveDb/40 proxy. Bit-identical
    // to v0.2.0 and therefore the default.
    drive = 0,

    // v0.3.0: an instant-attack / 30 ms-release peak envelope taken on the
    // oversampled clipper input.
    signal = 1,
};

// Clipper quality for the three memoryless voicings (ParamIDs::clipQuality).
// Values are persisted as an AudioParameterChoice index - append only.
enum class ClipQualityMode
{
    // The exact v0.2.0 clipper path, bit for bit. Default.
    classic = 0,

    // First-order antiderivative anti-aliasing (src/dsp/AdaaWaveshaper.h)
    // plus the 5 Hz DC blocker (src/dsp/DcBlocker.h).
    enhanced = 1,
};

// The complete Overture signal path, independent of juce::AudioProcessor
// so it can be exercised directly by unit tests without instantiating a
// full plugin (see tests/EngineTests.cpp). Owns all DSP state; every
// buffer/filter/oversampler is allocated in prepare() and never reallocated
// on the audio thread.
//
// Signal flow (v0.2.0 - see docs/design-brief.md and docs/architecture.md
// for the full diagram, the research-derived rationale for the changes
// below, and the latency-compensation rationale):
//
//   input -> Tight HPF -> Drive gain -> [Nx oversampled]
//              Bite shelf (frequency-dependent drive, INSIDE the gain path)
//              -> selectable clipper (Voicing, variable Asymmetry)
//              -> Knee Soften blend (drive-dependent)
//         -> Bite Tilt (post-clip bidirectional shelf, replaces v0.1's
//            cut-only Tone LPF) -> Level gain -> Dry/Wet mix -> output
//
// The dry path is delay-compensated against the oversampler's reported
// latency via juce::dsp::DryWetMixer, so Mix at 0% is a sample-accurate
// (once shifted by getLatencySamples()) passthrough of the input - this is
// what the plugin's null test (tests/EngineTests.cpp) exercises. None of
// this v0.1 oversampling/latency/dry-wet architecture changed in v0.2.0 -
// only what happens inside the Drive -> Clipper -> Tone/Bite portion of the
// chain (see docs/design-brief.md's "Topology (fixed)" section).
class OvertureEngine
{
public:
    OvertureEngine();

    // Allocates all DSP state, including (re)constructing the oversampler
    // at whatever factor was last requested via setOversamplingFactorPow2().
    // Must be called (and completed) before the first process() call, and
    // again whenever sample rate/block size/channel count/oversampling
    // factor change. Not real-time safe (allocates) - never call from the
    // audio thread.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all filter/oversampler/delay-line state without deallocating.
    // Safe to call from the audio thread (e.g. on playback stop/loop).
    void reset();

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here. If `block` exceeds the maximum sample count
    // declared to prepare() (spec.maximumBlockSize), it is defensively
    // split into chunks of at most that size before processing (issue #13)
    // rather than assumed - the oversampler's and DryWetMixer's internal
    // buffers are both fixed to that size and only guard the invariant
    // with a jassert, which compiles out in Release builds.
    void process (juce::dsp::AudioBlock<float>& block);

    // Parameter setters, in real units (dB, Hz, %, 0-1 proportion). Safe to
    // call every block from the audio thread - no allocation/locks. Drive
    // and Level are smoothed by the underlying juce::dsp::Gain ramp; every
    // other continuous control below is smoothed internally and re-applied
    // once per processed chunk (see processChunk()).
    void setDriveDb (float newDriveDb);
    void setTightFrequencyHz (float newFrequencyHz);
    void setLevelDb (float newLevelDb);
    void setMixProportion (float newProportion01);

    // Bite: frequency-dependent gain inside the drive-to-clipper gain path,
    // 0-100 (%). At 0, the clipper's drive is flat with frequency - a full
    // backward-compatible no-op (see docs/design-brief.md and
    // OvertureEngine.cpp's processChunk()).
    void setBiteAmountPercent (float newBiteAmountPercent);

    // Knee Soften: drive-dependent knee softening, 0-100 (%). At 0, every
    // voicing keeps its exact v0.1 fixed-knee transfer function at every
    // Drive level (see src/dsp/KneeSoftening.h).
    void setKneeSoftenPercent (float newKneeSoftenPercent);

    // Asymmetry: 0-100 (%), maps to the Asymmetric voicing's internal bias
    // `a` in 0.0-0.5. Only meaningful when the current Voicing is
    // Asymmetric (see ClipperVoicing.h) - the other two voicings ignore it,
    // as in v0.1's fixed a=0.2.
    void setAsymmetryAmountPercent (float newAsymmetryAmountPercent);

    // Bite Tilt: post-clip bidirectional shelf around a fixed ~3 kHz
    // corner, -100..+100 (%). 0 is flat (a true no-op - the filter is
    // skipped entirely, not just given unity-gain coefficients). Negative
    // darkens, positive brightens.
    void setBiteTiltPercent (float newBiteTiltPercent);

    // Selects the clipper nonlinearity. Real-time safe (just stores an
    // enum), but not smoothed/crossfaded - switching voicing is a discrete
    // mode change, not a continuously-automatable control, so an audible
    // step at the switch instant is expected and acceptable (the same way a
    // stompbox's voicing toggle clicks).
    void setClipperVoicing (ClipperVoicing newVoicing) noexcept;

    // Requests an oversampling factor of 2^newFactorPow2 (clamped to [1,3],
    // i.e. 2x/4x/8x). Reconstructing the internal juce::dsp::Oversampling
    // instance allocates, so this call only records the request - it takes
    // effect the next time prepare() runs, not instantaneously mid-stream.
    // Real-time safe to call (including from the audio thread) precisely
    // because it does NOT reallocate anything itself.
    void setOversamplingFactorPow2 (int newFactorPow2) noexcept;

    //==========================================================================
    // v0.3.0 additions. Every setter below defaults to the value that
    // reproduces v0.2.0 exactly, so an engine that is never told about them
    // behaves as it always did (tests/EngineTests.cpp's neutrality nulls).

    // Built-in noise gate master switch (src/dsp/NoiseGate.h). While false
    // the gate is skipped entirely - no detector, no sidechain filters, no
    // gain multiply - so the wet path is bit-identical to v0.2.0. The
    // false -> true transition re-seeds the gate's detector so engaging it
    // mid-performance neither clicks nor mutes the first few milliseconds
    // of a sustained note.
    void setGateEnabled (bool shouldBeEnabled) noexcept;

    // Gate opening threshold in dB (mean-square detector reference, so a
    // full-scale sine reads -3 dB). Smoothed internally over 20 ms.
    void setGateThresholdDb (float newThresholdDb) noexcept;

    // Gate release behaviour (Auto/Fast/Slow - see
    // basilica::dsp::NoiseGate::ReleaseMode).
    void setGateReleaseMode (basilica::dsp::NoiseGate::ReleaseMode newMode) noexcept;

    // Where Knee Soften's intensity factor comes from. Switching modes
    // pre-seeds the envelope from the current proxy value so the knee does
    // not step.
    void setKneeResponseMode (KneeResponseMode newMode) noexcept;

    // Classic (bit-identical v0.2.0 clipper) vs Enhanced (ADAA + DC
    // blocker) for the three memoryless voicings.
    void setClipQualityMode (ClipQualityMode newMode) noexcept;

    // Oversampling latency in samples, valid after prepare() has run. The
    // gate adds zero (no lookahead) and ADAA's half-sample delay is at the
    // OVERSAMPLED rate and deliberately not reported - see
    // src/dsp/AdaaWaveshaper.h.
    int getLatencySamples() const noexcept { return latencySamples; }

    // Sub-block size for parameter/coefficient updates, in base-rate
    // samples. Defaults to 32 (see subBlockSizeDefault). Exposed only so
    // tests/EngineTests.cpp (T-E1) can measure the v0.3.0 sub-block cadence
    // against the v0.2.0 block-rate cadence in the same binary - passing a
    // value >= the host block size reproduces exactly the v0.2.0 "one
    // coefficient update per block" behaviour. Not driven by any parameter.
    void setParameterUpdateSubBlockSize (int newSubBlockSize) noexcept;

    // Test/telemetry access to the gate's control path (never read by the
    // audio path itself) - tests/NoiseGateTests.cpp needs to separate the
    // detector's own settling from the hold/release timings it asserts.
    const basilica::dsp::NoiseGate& getNoiseGate() const noexcept { return noiseGate; }

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    // Butterworth (maximally-flat) Q for the 2nd-order Tight HPF, and reused
    // (same "no resonant peak" rationale) as the Q for the Bite shelf and
    // Bite Tilt shelf's 2nd-order RBJ-cookbook shelf filters below - JUCE
    // 8.0.14 juce::dsp::IIR::(Array)Coefficients::makeLowShelf/makeHighShelf
    // do not offer a true first-order (single-pole) shelf, so this uses the
    // standard 2nd-order shelf with the Q value that gives a maximally-flat
    // (non-resonant) plateau, the closest built-in equivalent to the
    // brief's "roughly first-order/6 dB-oct shelf slopes" target - see
    // docs/design-brief.md's Bite/Bite Tilt sections and
    // docs/architecture.md for the full citation.
    static constexpr float shelfQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    // Bite: fixed low-shelf corner anchored to the sourced ~720 Hz
    // reference-circuit feedback-loop corner (docs/research-notes.md SS3),
    // rounded per docs/design-brief.md's own "Bite" section. Not exposed as
    // a user control in v0.2.0.
    static constexpr float biteShelfCornerHz = 700.0f;

    // Maximum low-shelf cut (dB) applied to bass feeding the clipper at
    // biteAmount = 100%. Reasoned, not sourced to a specific number
    // (docs/design-brief.md flags biteAmount's intensity mapping as a
    // reasoned engineering choice) - 12 dB is a moderate, clearly audible
    // bass reduction into the clipper without fully removing low-end
    // content from the nonlinearity.
    static constexpr float biteShelfMaxCutDb = 12.0f;

    // Bite Tilt: fixed high-shelf corner anchored to the sourced ~3.2 kHz
    // reference-circuit tone-tilt corner (docs/research-notes.md SS3),
    // rounded to 3 kHz per docs/design-brief.md's "Bite" section.
    static constexpr float biteTiltCornerHz = 3000.0f;

    // Maximum tilt (dB) applied at biteTilt = -100%, i.e. the CUT
    // direction. Reasoned, not sourced - chosen generously (unlike a subtle
    // "mix bus" tilt EQ) so that the fully-negative setting comfortably
    // subsumes v0.1's entire cut-only Tone range (a hard backward-
    // compatibility guarantee from docs/design-brief.md - see
    // tests/EngineTests.cpp's bidirectionality test, which measures this
    // directly rather than assuming it). A 2nd-order RBJ shelf (see shelfQ's
    // docs) only asymptotically approaches this figure well above the
    // corner, so the constant itself has to be considerably larger than the
    // darkness actually wanted at any single audible test frequency -
    // 100 dB was tuned empirically against tests/EngineTests.cpp's
    // "subsumes v0.1's Tone range" comparison at 4 kHz (2 octaves above
    // v0.1's darkest 1 kHz Tone cutoff).
    static constexpr float biteTiltMaxCutDb = 100.0f;

    // Maximum tilt (dB) applied at biteTilt = +100%, i.e. the BOOST
    // direction - and deliberately NOT the same number as the cut above.
    //
    // Until v0.3.1 a single symmetric constant served both directions, so
    // the 100 dB that the cut side needs for its backward-compatibility
    // guarantee was also what the boost side delivered: Bite Tilt applied
    // very nearly one dB of broadband boost per percent of knob travel,
    // because the programme material that matters here carries energy well
    // above the 3 kHz corner where the shelf IS asymptotic. Measured: the
    // Fuzz-Adjacent Lead factory preset, whose Hard Clip voicing bounds the
    // pre-tilt signal at 0 dBFS by construction, rendered the reference
    // programme at +27.10 dBFS - of which +23.27 dB was this shelf at a
    // Bite Tilt of +25%. That is not a preset that was voiced too hot; it
    // is a tone control whose upper half was an unexamined mirror of a
    // constant tuned for its lower half. Nothing in docs/design-brief.md
    // ever asked the boost direction for more than "brighter".
    //
    // 12 dB is this plugin's own reasoned magnitude for a shelf that is
    // "moderate and clearly audible" - it is exactly biteShelfMaxCutDb
    // above, so Overture's two tone shelves now have the same authority as
    // each other. It also bounds how much output headroom a positive tilt
    // can consume at 12 dB, which is inside what the Level control's
    // +/-24 dB range can give back.
    static constexpr float biteTiltMaxBoostDb = 12.0f;

    // Asymmetry: `asymmetryAmount` (0-100%) maps linearly to the
    // Asymmetric voicing's internal bias `a` in 0.0-this value. 40% (the
    // parameter's default) * 0.5 = 0.2, exactly reproducing v0.1's fixed
    // a=0.2 default - see docs/design-brief.md's "asymmetry_amount"
    // section for the reasoned (not measured) sourcing of the 0.5 ceiling.
    static constexpr float asymmetryMaxBias = 0.5f;

    // Reference Drive value (dB) used to normalise the "how hard is the
    // clipper currently being driven" proxy that scales Knee Soften's
    // effect (see processChunk()) to 0-1 - the top of Drive's own 0-40 dB
    // range, so Knee Soften reaches its full per-control effect exactly at
    // maximum Drive.
    static constexpr float driveIntensityReferenceDb = 40.0f;

    double sampleRate = 44100.0;

    // Rest-flush machinery (fleet audit class 2b, issue #35) - see
    // process(). Threshold = the exact value juce_dsp's own per-block
    // snapToZero() pass used (JUCE_SNAP_TO_ZERO,
    // juce_FloatVectorOperations.h, JUCE 8.0.14) before the fleet disabled
    // JUCE_DSP_ENABLE_SNAP_TO_ZERO; dwell = one second of contiguous
    // silent input at the prepared rate, an order-of-magnitude upper bound
    // over the oversampler/DryWetMixer latency, so no in-flight audio can
    // be swallowed.
    static constexpr float restFlushThreshold = 1.0e-8f;
    juce::int64 restFlushDwellSamples = 48000;
    juce::int64 silentInputStreak = 0;
    bool restFlushed = false;

    // The oversampled processing rate (sampleRate * the oversampler's
    // actual factor, e.g. 4x), computed once in prepare() once the
    // oversampler has been (re)constructed - the Bite shelf runs on the
    // oversampled block (see processChunk()), so its coefficients must be
    // derived against this rate, not the base sampleRate above.
    double oversampledSampleRate = 44100.0;

    // Requested oversampling factor as a power of two (1 => 2x, 2 => 4x,
    // 3 => 8x); 2 (4x) matches the fixed factor the v0.1 engine always used.
    // Only consumed by prepare() - see setOversamplingFactorPow2().
    int oversamplingFactorPow2 = 2;

    ClipperVoicing currentVoicing = ClipperVoicing::asymmetric;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tightHighPass;
    juce::dsp::Gain<float> driveGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // Bite: low-shelf INSIDE the oversampled drive-to-clipper path (see the
    // class-level signal-flow diagram above) - runs on the up-sampled
    // block, immediately before the per-sample voicing dispatch loop, and
    // only when biteAmount > 0 (see processChunk()).
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> biteShelf;

    // Bite Tilt: post-clip, post-downsample bidirectional high-shelf that
    // replaces v0.1's two cascaded 4th-order-Butterworth-forming Tone
    // low-pass sections. A single 2nd-order shelf is enough for a tilt
    // control (see the shelfQ docs above) - only applied when biteTilt != 0
    // (see processChunk()), so the flat/0% default is a true no-op rather
    // than a unity-gain filter call.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> biteTiltShelf;

    juce::dsp::Gain<float> outputLevel;

    // Sized generously above any realistic oversampling latency (even at
    // the maximum 8x factor, half-band polyphase IIR latency stays in the
    // single-digit-to-low-tens-of-samples range - empirically ~6 samples at
    // 48 kHz, see tests/LatencyTests.cpp's oversampling-factor coverage) so
    // setWetLatency() never exceeds the mixer's internal delay-line
    // capacity regardless of sample rate or factor.
    juce::dsp::DryWetMixer<float> dryWetMixer { 1024 };

    // Frequency parameters use multiplicative smoothing (appropriate for
    // quantities that are perceived logarithmically, like Hz); every other
    // continuous control here (percentages, proportions, +/- tilt) uses
    // linear smoothing.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> tightFrequencySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> biteAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> kneeSoftenSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> asymmetryAmountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> biteTiltSmoothed;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied to the smoothers on every prepare() so re-prepare
    // (sample-rate change, etc.) never resets a live parameter back to a
    // default or lets a smoother start from an invalid 0 Hz. Mirrors the
    // v0.2.0 ParameterLayout defaults (see src/params/ParameterLayout.cpp)
    // so an engine used standalone (as in most of tests/EngineTests.cpp)
    // without an explicit setter call still starts from a sane,
    // amp-front-end-tuned value rather than an arbitrary placeholder.
    float lastTightHz = 100.0f;
    float lastMixProportion = 1.0f;
    float lastBiteAmountPercent = 65.0f;
    float lastKneeSoftenPercent = 40.0f;
    float lastAsymmetryAmountPercent = 40.0f;
    float lastBiteTiltPercent = 0.0f;

    // The last commanded Drive value (dB) - not itself smoothed here (Drive
    // is smoothed by driveGain's own internal juce::dsp::Gain ramp, which
    // doesn't expose a "current ramped value" getter); used as a real-time-
    // safe, block-rate proxy for "how hard is the clipper currently being
    // driven" that scales Knee Soften's effect (see processChunk() and
    // src/dsp/KneeSoftening.h's docs) - the same "one block-rate snapshot
    // per chunk" compromise Tight/Bite/Bite Tilt's own coefficient
    // recomputation already makes.
    float lastDriveDb = 3.0f;

    int latencySamples = 0;

    // Maximum block size promised to prepare() (spec.maximumBlockSize); 0
    // until the first prepare() call. process() uses this to defensively
    // chunk any incoming block larger than what the oversampler's and
    // DryWetMixer's internal buffers were actually sized for (issue #13).
    size_t preparedMaxBlockSize = 0;

    //==========================================================================
    // v0.3.0 state.

    size_t subBlockSize = subBlockSizeDefault;

    // Built-in noise gate. The detector taps the plugin INPUT (pre-Tight,
    // pre-Drive - the cleanest signal available in-plugin, analogous to
    // keying from the instrument); the gain is applied to the wet path
    // input, before the Tight HPF, so gated noise never reaches the
    // clipper. The dry path (Mix < 100%) is deliberately ungated - the gate
    // is part of the wet pedal chain (documented in docs/manual.md).
    // Stereo linked: the detector sees the max of |x| across channels and
    // one gain is applied to both.
    basilica::dsp::NoiseGate noiseGate;
    bool gateEnabled = false;
    bool lastGateEnabled = false;
    float lastGateThresholdDb = -50.0f;
    basilica::dsp::NoiseGate::ReleaseMode gateReleaseMode = basilica::dsp::NoiseGate::ReleaseMode::automatic;

    // Circuit-solved Feedback voicing (ClipperVoicing::feedback). Runs
    // INSIDE the oversampler, replacing the Bite-shelf -> memoryless-clipper
    // path entirely: Bite and Knee Soften have no effect in this voicing
    // (the circuit's own 720 Hz pre-emphasis IS the bite mechanism and its
    // knee is physical), and the plain Drive gain stage is forced to 0 dB
    // because the circuit computes its own 21.5-41.4 dB in-band loop gain.
    basilica::dsp::FeedbackClipperStage feedbackStage;

    // "Enhanced" clip quality: antiderivative anti-aliasing around the three
    // memoryless voicings, plus the DC blocker below.
    basilica::dsp::AdaaWaveshaper adaaWaveshaper;
    ClipQualityMode clipQualityMode = ClipQualityMode::classic;

    // Post-downsample, pre-Bite-Tilt. Active when clipQuality is Enhanced
    // OR the Feedback voicing is selected.
    basilica::dsp::DcBlocker dcBlocker;

    // "Signal" knee response: an instant-attack / 30 ms-release peak
    // follower on the OVERSAMPLED clipper input (one per channel).
    basilica::dsp::MultiChannelPeakFollower kneeEnvelope;
    KneeResponseMode kneeResponseMode = KneeResponseMode::drive;

    // Peak-follower release time constant for the Signal knee response.
    static constexpr double kneeEnvelopeReleaseSeconds = 0.030;

    // Coefficient-update gating (see subBlockSizeDefault): the smoothed
    // values the currently-loaded filter coefficients were derived from.
    // Initialised to an impossible sentinel in prepare() so the first
    // sub-block after every prepare() always recomputes - that is what keeps
    // the running coefficients on the ArrayCoefficients path rather than on
    // prepare()'s (ULP-different) allocating IIR::Coefficients priming.
    float lastAppliedTightHz = -1.0f;
    float lastAppliedBiteAmountPercent = -1.0f;
    float lastAppliedBiteTiltPercent = -1.0e9f;

    // Recompute thresholds. Small enough to be inaudible, large enough that
    // a settled smoother (delta exactly 0) never re-derives coefficients -
    // which is what preserves v0.2.0's steady-state bit-identity.
    static constexpr float coefficientEpsilonHz = 1.0e-4f;
    static constexpr float coefficientEpsilonPercent = 1.0e-4f;

    // v0.3.0: parameter/coefficient update cadence. v0.2.0 recomputed the
    // Tight HPF, Bite shelf and Bite Tilt coefficients exactly once per host
    // block (~86 Hz at 512 samples/48 kHz), which is audible as zipper noise
    // when Bite Tilt or Tight is automated. Splitting the base-rate work
    // into 32-sample sub-blocks raises that cadence to 1.5 kHz at the same
    // sample rate, at the cost of one extra oversampler up/down call per
    // sub-block - the IIR paths are block-size invariant, so the split is
    // bit-transparent (asserted in tests/EngineTests.cpp).
    static constexpr size_t subBlockSizeDefault = 32;

    // Runs the full Gate -> Tight -> Drive -> oversampled Bite/Voicing/Knee
    // Soften -> DC blocker -> Bite Tilt -> Level -> Mix chain in place on a
    // single chunk of at most preparedMaxBlockSize samples, splitting it
    // into sub-blocks (see subBlockSizeDefault). Called once per chunk by
    // process(), which is what performs the size-based splitting.
    void processChunk (juce::dsp::AudioBlock<float>& block);

    // One sub-block of the chain above. All parameter smoothing, coefficient
    // recomputation and mode dispatch happens here.
    void processSubBlock (juce::dsp::AudioBlock<float>& block);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureEngine)
};
