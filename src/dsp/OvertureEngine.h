#pragma once

#include <juce_dsp/juce_dsp.h>

#include "AsymSoftClipper.h"

// The complete Overture signal path, independent of juce::AudioProcessor
// so it can be exercised directly by unit tests without instantiating a
// full plugin (see tests/EngineTests.cpp). Owns all DSP state; every
// buffer/filter/oversampler is allocated in prepare() and never reallocated
// on the audio thread.
//
// Signal flow (see docs/architecture.md for the full diagram and the
// latency-compensation rationale):
//
//   input -> Tight HPF -> Drive gain -> [4x oversampled] asym soft clip
//         -> Tone LPF -> Level gain -> Dry/Wet mix -> output
//
// The dry path is delay-compensated against the oversampler's reported
// latency via juce::dsp::DryWetMixer, so Mix at 0% is a sample-accurate
// (once shifted by getLatencySamples()) passthrough of the input - this is
// what the plugin's null test (tests/EngineTests.cpp) exercises.
class OvertureEngine
{
public:
    OvertureEngine();

    // Allocates all DSP state. Must be called (and completed) before the
    // first process() call, and again whenever sample rate/block size/
    // channel count change.
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

    // Oversampling latency in samples, valid after prepare() has run.
    int getLatencySamples() const noexcept { return latencySamples; }

private:
    static constexpr int oversamplingFactorPow2 = 2; // 2^2 = 4x oversampling
    static constexpr double smoothingTimeSeconds = 0.05;
    static constexpr float clipperAsymmetry = 0.2f;
    // Butterworth (maximally-flat) Q for both the Tight HPF and Tone LPF.
    static constexpr float filterQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    double sampleRate = 44100.0;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tightHighPass;
    juce::dsp::Gain<float> driveGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> toneLowPass;
    juce::dsp::Gain<float> outputLevel;

    // Sized generously above any realistic oversampling latency (4x
    // half-band polyphase IIR latency is on the order of tens of samples)
    // so setWetLatency() never exceeds the mixer's internal delay-line
    // capacity regardless of sample rate.
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
    float lastTightHz = 150.0f;
    float lastToneHz = 5000.0f;
    float lastMixProportion = 1.0f;

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureEngine)
};
