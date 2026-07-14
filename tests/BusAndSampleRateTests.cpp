#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Broadens coverage beyond the original v0.1 suite per the M1 "Broaden test
// coverage" issue: sample-rate sweeps (44.1-192 kHz), mono/stereo bus
// configurations, and long-run NaN/Inf stability. Extreme parameter
// automation already has dedicated coverage in RobustnessTests.cpp; this
// file adds the same automation loop across the new Bypass/Voicing/
// Oversampling parameters specifically.
namespace
{
    void setParam (OvertureAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Sample-rate sweep 44.1-192 kHz: finite output and valid positive latency at every rate", "[robustness][samplerate]")
{
    static constexpr double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    for (const auto sampleRate : sampleRates)
    {
        OvertureAudioProcessor processor;
        processor.prepareToPlay (sampleRate, 256);

        CHECK (processor.getLatencySamples() > 0);

        setParam (processor, ParamIDs::drive, 25.0f);
        setParam (processor, ParamIDs::tight, 200.0f);
        setParam (processor, ParamIDs::tone, 4000.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, sampleRate, 1000.0, 0.7f);

        juce::MidiBuffer midi;

        for (int block = 0; block < 4; ++block)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Sample-rate change mid-session (prepareToPlay called again) stays finite", "[robustness][samplerate]")
{
    OvertureAudioProcessor processor;
    juce::MidiBuffer midi;

    static constexpr double sampleRates[] = { 44100.0, 192000.0, 48000.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        processor.prepareToPlay (sampleRate, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.6f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Mono bus layout is supported and processes without NaN/Inf", "[robustness][buslayout]")
{
    OvertureAudioProcessor processor;

    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add (juce::AudioChannelSet::mono());

    REQUIRE (processor.isBusesLayoutSupported (monoLayout));
    REQUIRE (processor.setBusesLayout (monoLayout));

    processor.prepareToPlay (48000.0, 256);

    setParam (processor, ParamIDs::drive, 30.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (1, 256);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Stereo bus layout is supported (explicit isBusesLayoutSupported check)", "[robustness][buslayout]")
{
    OvertureAudioProcessor processor;

    juce::AudioProcessor::BusesLayout stereoLayout;
    stereoLayout.inputBuses.add (juce::AudioChannelSet::stereo());
    stereoLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK (processor.isBusesLayoutSupported (stereoLayout));
}

TEST_CASE ("Mismatched in/out channel-set bus layouts are rejected", "[robustness][buslayout]")
{
    OvertureAudioProcessor processor;

    juce::AudioProcessor::BusesLayout mismatchedLayout;
    mismatchedLayout.inputBuses.add (juce::AudioChannelSet::mono());
    mismatchedLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK_FALSE (processor.isBusesLayoutSupported (mismatchedLayout));
}

TEST_CASE ("Unsupported multichannel bus layout is rejected", "[robustness][buslayout]")
{
    OvertureAudioProcessor processor;

    juce::AudioProcessor::BusesLayout quadLayout;
    quadLayout.inputBuses.add (juce::AudioChannelSet::quadraphonic());
    quadLayout.outputBuses.add (juce::AudioChannelSet::quadraphonic());

    CHECK_FALSE (processor.isBusesLayoutSupported (quadLayout));
}

TEST_CASE ("Long-run processing (many blocks, several seconds of audio) produces no NaN/Inf drift", "[robustness][longrun]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 28.0f);
    setParam (processor, ParamIDs::tight, 180.0f);
    setParam (processor, ParamIDs::tone, 3500.0f);
    setParam (processor, ParamIDs::level, 6.0f);
    setParam (processor, ParamIDs::mix, 85.0f);

    juce::MidiBuffer midi;

    // 500 blocks @ 512 samples/48kHz ~= 5.3 seconds of continuous audio -
    // long enough to reveal slow-building filter-state or smoother drift
    // while staying comfortably under a minute even on Debug/Windows CI.
    constexpr int numBlocks = 500;

    for (int block = 0; block < numBlocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 110.0, 0.75f, static_cast<juce::int64> (block) * 512);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 100.0f);
    }
}

TEST_CASE ("Rapid Bypass/Voicing/Oversampling automation across many blocks produces no NaN/Inf", "[robustness][automation]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    juce::MidiBuffer midi;

    for (int block = 0; block < 60; ++block)
    {
        setParam (processor, ParamIDs::bypass, (block % 5 == 0) ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::voicing, static_cast<float> (block % 3));
        // Oversampling only takes effect on the next prepareToPlay(), so
        // this is exercising "does setting it mid-stream ever crash",
        // not "does it change latency instantaneously".
        setParam (processor, ParamIDs::oversampling, static_cast<float> (block % 3));
        setParam (processor, ParamIDs::drive, static_cast<float> (block % 40));

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 500.0 + static_cast<double> (block) * 10.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}
