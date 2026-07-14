#include "PluginProcessor.h"
#include "dsp/OvertureEngine.h"
#include "params/ParameterIds.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("getLatencySamples() reports the oversampling latency after prepareToPlay", "[latency]")
{
    OvertureAudioProcessor processor;

    // Before prepareToPlay, no engine has been prepared yet - JUCE's default
    // AudioProcessor latency is 0.
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    // Cross-check against a standalone engine prepared identically: the
    // processor must report exactly what the engine (i.e. the oversampler)
    // computes, not an approximation of it.
    OvertureEngine referenceEngine;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    referenceEngine.prepare (spec);

    CHECK (processor.getLatencySamples() == referenceEngine.getLatencySamples());
    CHECK (processor.getLatencySamples() > 0); // 4x oversampling always has some latency
}

TEST_CASE ("Latency is stable across repeated prepareToPlay calls at the same sample rate", "[latency]")
{
    OvertureAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    const auto firstLatency = processor.getLatencySamples();

    processor.prepareToPlay (44100.0, 256);
    const auto secondLatency = processor.getLatencySamples();

    CHECK (firstLatency == secondLatency);
}

TEST_CASE ("Latency updates correctly when the sample rate changes", "[latency]")
{
    OvertureAudioProcessor processor;

    processor.prepareToPlay (44100.0, 512);
    const auto latencyAt44k = processor.getLatencySamples();

    processor.prepareToPlay (96000.0, 512);
    const auto latencyAt96k = processor.getLatencySamples();

    CHECK (latencyAt44k > 0);
    CHECK (latencyAt96k > 0);
    // Not asserting a specific ratio (that depends on JUCE's internal
    // half-band filter design), just that both are well-defined positive
    // latencies reported consistently.
}

TEST_CASE ("Oversampling factor 2x/4x/8x each report valid, non-decreasing latency", "[latency][oversampling]")
{
    // Engine-level, so the factor can be set directly via
    // setOversamplingFactorPow2() without going through the Oversampling
    // choice parameter - see OversamplingFactorTests below for the
    // processor-level (APVTS-driven) equivalent.
    //
    // NOT asserting strictly increasing latency per factor: JUCE 8.0.14's
    // juce::dsp::Oversampling, with useIntegerLatency=true (as OvertureEngine
    // uses), rounds each cascaded 2x stage's fractional latency to the
    // nearest integer sample independently, which can make an additional
    // stage not increase the *rounded* total - empirically, at 48 kHz this
    // engine measures 4 samples at 2x, 6 samples at both 4x and 8x. The
    // invariant that actually holds (and is what OvertureEngine's
    // DryWetMixer capacity/null-test tolerance rely on) is monotonic
    // non-decrease, which this test verifies.
    int previousLatency = -1;

    for (int factorPow2 : { 1, 2, 3 }) // 2x, 4x, 8x
    {
        OvertureEngine engine;
        engine.setOversamplingFactorPow2 (factorPow2);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = 512;
        spec.numChannels = 2;
        engine.prepare (spec);

        const auto latency = engine.getLatencySamples();

        CHECK (latency > 0);
        // Sanity bound: must stay well inside DryWetMixer's fixed 1024-
        // sample dry-delay capacity (see OvertureEngine.h) even at the
        // maximum 8x factor.
        CHECK (latency < 512);
        CHECK (latency >= previousLatency); // higher factor -> never less latency
        previousLatency = latency;
    }

    // The two extremes must differ: 8x is strictly more oversampled work
    // than 2x, and empirically does report more latency even though the
    // 4x->8x step alone can plateau (see comment above).
    OvertureEngine engine2x;
    engine2x.setOversamplingFactorPow2 (1);
    OvertureEngine engine8x;
    engine8x.setOversamplingFactorPow2 (3);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    engine2x.prepare (spec);
    engine8x.prepare (spec);

    CHECK (engine8x.getLatencySamples() > engine2x.getLatencySamples());
}

TEST_CASE ("setOversamplingFactorPow2() clamps out-of-range requests", "[latency][oversampling]")
{
    OvertureEngine engineTooLow;
    engineTooLow.setOversamplingFactorPow2 (0);

    OvertureEngine engineReference1x;
    engineReference1x.setOversamplingFactorPow2 (1);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;

    engineTooLow.prepare (spec);
    engineReference1x.prepare (spec);

    // Requesting 2^0 (1x, i.e. no oversampling) is out of the supported
    // [1,3] range and must clamp to the minimum (2x), not silently disable
    // oversampling or produce an invalid state.
    CHECK (engineTooLow.getLatencySamples() == engineReference1x.getLatencySamples());

    OvertureEngine engineTooHigh;
    engineTooHigh.setOversamplingFactorPow2 (10);

    OvertureEngine engineReference8x;
    engineReference8x.setOversamplingFactorPow2 (3);

    engineTooHigh.prepare (spec);
    engineReference8x.prepare (spec);

    CHECK (engineTooHigh.getLatencySamples() == engineReference8x.getLatencySamples());
}

TEST_CASE ("OversamplingFactorTests: processor-level Oversampling parameter changes latency on the next prepareToPlay", "[latency][oversampling][processor]")
{
    OvertureAudioProcessor processor;

    auto* oversamplingParam = processor.apvts.getParameter (ParamIDs::oversampling);
    REQUIRE (oversamplingParam != nullptr);

    // Default (index 1, "4x").
    processor.prepareToPlay (48000.0, 512);
    const auto latencyAt4x = processor.getLatencySamples();
    CHECK (latencyAt4x > 0);

    // Switch to "2x" (index 0) - per OvertureEngine::setOversamplingFactorPow2's
    // contract, this does NOT take effect until the next prepareToPlay().
    // (2x vs. 4x is used here, not 4x vs. 8x, because JUCE 8.0.14's
    // integer-latency rounding can make 4x and 8x report the same latency -
    // see the plateau documented in the "non-decreasing latency" test above;
    // 2x vs. 4x reliably differs.)
    oversamplingParam->setValueNotifyingHost (oversamplingParam->convertTo0to1 (0.0f));
    CHECK (processor.getLatencySamples() == latencyAt4x); // unchanged yet

    processor.prepareToPlay (48000.0, 512);
    const auto latencyAt2x = processor.getLatencySamples();

    CHECK (latencyAt2x < latencyAt4x);
}
