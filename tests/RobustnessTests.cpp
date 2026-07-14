#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

namespace
{
    void setParam (OvertureAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 40.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Full-scale input at maximum drive produces no NaN/Inf", "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 40.0f);
    setParam (processor, ParamIDs::tight, 400.0f);
    setParam (processor, ParamIDs::tone, 8000.0f);
    setParam (processor, ParamIDs::level, 24.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 100.0f); // sane bound, not just "finite"
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 20.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        setParam (processor, ParamIDs::tight, useMinimum ? 20.0f : 400.0f);
        setParam (processor, ParamIDs::drive, useMinimum ? 0.0f : 40.0f);
        setParam (processor, ParamIDs::tone, useMinimum ? 1000.0f : 8000.0f);
        setParam (processor, ParamIDs::level, useMinimum ? -24.0f : 24.0f);
        setParam (processor, ParamIDs::mix, useMinimum ? 0.0f : 100.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::tight, 20.0f + unit (rng) * 380.0f);
        setParam (processor, ParamIDs::drive, unit (rng) * 40.0f);
        setParam (processor, ParamIDs::tone, 1000.0f + unit (rng) * 7000.0f);
        setParam (processor, ParamIDs::level, -24.0f + unit (rng) * 48.0f);
        setParam (processor, ParamIDs::mix, unit (rng) * 100.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Bypass parameter forces a delay-compensated passthrough regardless of other parameters", "[robustness][bypass]")
{
    // Mirrors EngineTests.cpp's "0% mix nulls against the input" null test,
    // but drives the *host-visible Bypass parameter* end-to-end through
    // OvertureAudioProcessor rather than calling OvertureEngine::setMixProportion()
    // directly, proving the processBlock()-level wiring (getBypassParameter(),
    // the bypassFlag atomic, and the mix override) actually works.
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 8192);

    setParam (processor, ParamIDs::bypass, 1.0f);
    // Deliberately non-neutral settings elsewhere: a true bypass test has to
    // prove the *entire* wet chain is bypassed, not just quiet by default.
    setParam (processor, ParamIDs::drive, 30.0f);
    setParam (processor, ParamIDs::tight, 300.0f);
    setParam (processor, ParamIDs::tone, 2000.0f);
    setParam (processor, ParamIDs::level, 12.0f);
    setParam (processor, ParamIDs::mix, 100.0f); // Mix itself says "fully wet" - Bypass must override it

    const auto latency = processor.getLatencySamples();
    REQUIRE (latency > 0);
    REQUIRE (latency < 8192 / 2);

    juce::MidiBuffer midi;

    // Unlike prepare()'s Mix priming (which snaps the DryWetMixer's
    // internal smoother's current value straight to its target - see
    // OvertureEngine::prepare()), engaging Bypass mid-stream goes through
    // the *normal*, intentionally-smoothed Mix path so a live bypass toggle
    // crossfades instead of clicking. That means it takes both the engine's
    // ~50ms mixSmoothed ramp and the DryWetMixer's own ~50ms internal ramp
    // (see tests/DryWetMixerContractTests.cpp) to fully settle to null -
    // worst case, close to 100ms. Warm up for well over that (~340ms, two
    // 8192-sample blocks at 48kHz) before measuring, the same "let it
    // settle before measuring" technique EngineTests.cpp's near-linear test
    // uses for the Tight HPF's turn-on transient.
    juce::AudioBuffer<float> warmup (2, 8192);
    TestHelpers::fillWithSine (warmup, 48000.0, 1000.0, 0.5f, 0);
    processor.processBlock (warmup, midi);
    TestHelpers::fillWithSine (warmup, 48000.0, 1000.0, 0.5f, 8192);
    processor.processBlock (warmup, midi);

    juce::AudioBuffer<float> reference (2, 8192);
    TestHelpers::fillWithSine (reference, 48000.0, 1000.0, 0.5f, 2 * 8192);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    processor.processBlock (processed, midi);

    const auto overlapLength = 8192 - latency;
    REQUIRE (overlapLength > 8192 / 2);

    constexpr float tolerance = 3.1623e-5f; // < -90 dBFS residual

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        float maxResidual = 0.0f;

        for (int i = 0; i < overlapLength; ++i)
            maxResidual = std::max (maxResidual, std::abs (outData[latency + i] - refData[i]));

        CHECK (maxResidual < tolerance);
    }
}

TEST_CASE ("Toggling Bypass on and off across many blocks produces no NaN/Inf", "[robustness][bypass]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    setParam (processor, ParamIDs::drive, 25.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 50; ++block)
    {
        setParam (processor, ParamIDs::bypass, (block % 2 == 0) ? 1.0f : 0.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Each clipper voicing produces no NaN/Inf at maximum drive", "[robustness][voicing]")
{
    for (float voicingIndex : { 0.0f, 1.0f, 2.0f })
    {
        OvertureAudioProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        setParam (processor, ParamIDs::voicing, voicingIndex);
        setParam (processor, ParamIDs::drive, 40.0f);
        setParam (processor, ParamIDs::mix, 100.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

        juce::MidiBuffer midi;

        for (int i = 0; i < 8; ++i)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Each oversampling factor produces no NaN/Inf at maximum drive", "[robustness][oversampling]")
{
    for (float oversamplingIndex : { 0.0f, 1.0f, 2.0f }) // 2x, 4x, 8x
    {
        OvertureAudioProcessor processor;

        setParam (processor, ParamIDs::oversampling, oversamplingIndex);
        processor.prepareToPlay (48000.0, 512); // factor takes effect here

        setParam (processor, ParamIDs::drive, 40.0f);
        setParam (processor, ParamIDs::mix, 100.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

        juce::MidiBuffer midi;

        for (int i = 0; i < 8; ++i)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 30.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}
