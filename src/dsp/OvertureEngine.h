#pragma once

#include <juce_dsp/juce_dsp.h>

#include "AsymSoftClipper.h"
#include "ClipperVoicing.h"

// The complete Overture signal path, independent of juce::AudioProcessor
// so it can be exercised directly by unit tests without instantiating a
// full plugin (see tests/EngineTests.cpp). Owns all DSP state; every
// buffer/filter/oversampler is allocated in prepare() and never reallocated
// on the audio thread.
//
// Signal flow (see docs/architecture.md for the full diagram and the
// latency-compensation rationale):
//
//   input -> Tight HPF -> Drive gain -> [Nx oversampled] selectable clipper
//         -> Tone LPF (4th-order cascade) -> Level gain -> Dry/Wet mix -> output
//
// The dry path is delay-compensated against the oversampler's reported
// latency via juce::dsp::DryWetMixer, so Mix at 0% is a sample-accurate
// (once shifted by getLatencySamples()) passthrough of the input - this is
// what the plugin's null test (tests/EngineTests.cpp) exercises.
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

    // Processes `block` in place. `block` must have at most the maximum
    // sample/channel counts declared to prepare(); a zero-sample block is a
    // safe no-op. No allocation occurs here.
    void process (juce::dsp::AudioBlock<float>& block);

    // Parameter setters, in real units (dB, Hz, 0-1 proportion). Safe to
    // call every block from the audio thread - no allocation/locks. Drive
    // and Level are smoothed by the underlying juce::dsp::Gain ramp;
    // Tight/Tone/Mix are smoothed internally and re-applied once per block
    // (see process()).
    void setDriveDb (float newDriveDb);
    void setTightFrequencyHz (float newFrequencyHz);
    void setToneFrequencyHz (float newFrequencyHz);
    void setLevelDb (float newLevelDb);
    void setMixProportion (float newProportion01);

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

    // Oversampling latency in samples, valid after prepare() has run.
    int getLatencySamples() const noexcept { return latencySamples; }

private:
    static constexpr double smoothingTimeSeconds = 0.05;
    static constexpr float clipperAsymmetry = 0.2f;
    // Butterworth (maximally-flat) Q for the 2nd-order Tight HPF.
    static constexpr float tightFilterQ = juce::MathConstants<float>::sqrt2 / 2.0f;
    // Q values for a 4th-order Butterworth low-pass built as a cascade of
    // two 2nd-order IIR sections at the same cutoff frequency (standard
    // filter-cookbook values, e.g. Zolzer, DAFX, table for order-4
    // Butterworth cascades). Steeper (24 dB/oct vs. the previous 12 dB/oct)
    // roll-off tames post-clipper fizz more effectively without moving the
    // -3 dB point.
    static constexpr float toneFilterQ1 = 0.5411961f;
    static constexpr float toneFilterQ2 = 1.3065630f;

    double sampleRate = 44100.0;

    // Requested oversampling factor as a power of two (1 => 2x, 2 => 4x,
    // 3 => 8x); 2 (4x) matches the fixed factor the v0.1 engine always used.
    // Only consumed by prepare() - see setOversamplingFactorPow2().
    int oversamplingFactorPow2 = 2;

    ClipperVoicing currentVoicing = ClipperVoicing::asymmetric;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tightHighPass;
    juce::dsp::Gain<float> driveGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    // Tone is a 4th-order Butterworth low-pass, built as two cascaded
    // 2nd-order sections at the same cutoff (toneFilterQ1/toneFilterQ2)
    // rather than a single 2nd-order section - see the Q constants above.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> toneLowPassStage1;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> toneLowPassStage2;
    juce::dsp::Gain<float> outputLevel;

    // Sized generously above any realistic oversampling latency (even at
    // the maximum 8x factor, half-band polyphase IIR latency stays in the
    // single-digit-to-low-tens-of-samples range - empirically ~6 samples at
    // 48 kHz, see tests/LatencyTests.cpp's oversampling-factor coverage) so
    // setWetLatency() never exceeds the mixer's internal delay-line
    // capacity regardless of sample rate or factor.
    juce::dsp::DryWetMixer<float> dryWetMixer { 1024 };

    // Frequency parameters use multiplicative smoothing (appropriate for
    // quantities that are perceived logarithmically, like Hz); Mix uses
    // linear smoothing and must be able to reach exactly 0.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> tightFrequencySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> toneFrequencySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied to the smoothers on every prepare() so re-prepare
    // (sample-rate change, etc.) never resets a live parameter back to a
    // default or lets a smoother start from an invalid 0 Hz.
    // Mirrors the v0.1.0 ParameterLayout defaults (see
    // src/params/ParameterLayout.cpp) so an engine used standalone (as in
    // most of tests/EngineTests.cpp) without an explicit setter call still
    // starts from a sane, amp-front-end-tuned value rather than an
    // arbitrary placeholder.
    float lastTightHz = 130.0f;
    float lastToneHz = 6000.0f;
    float lastMixProportion = 1.0f;

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureEngine)
};
