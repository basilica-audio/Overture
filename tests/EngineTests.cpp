#include "dsp/OvertureEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 8192; // large single block: keeps the null/
                                         // correlation tests below simple by
                                         // avoiding multi-block bookkeeping.
    constexpr double testFrequencyHz = 1000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Engine null test: 0% mix nulls against the input once shifted by latency", "[dsp][engine][null]")
{
    OvertureEngine engine;

    // Parameters other than Mix are deliberately set to non-neutral values:
    // a true null test has to prove the *entire* wet chain is bypassed, not
    // just that it happens to be quiet at default settings.
    engine.setMixProportion (0.0f);
    engine.setDriveDb (25.0f);
    engine.setTightFrequencyHz (300.0f);
    engine.setToneFrequencyHz (2000.0f);
    engine.setLevelDb (10.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    const auto latency = engine.getLatencySamples();
    REQUIRE (latency >= 0);
    // Sanity bound: the oversampling latency must be well inside both the
    // DryWetMixer's fixed dry-delay capacity (1024, see OvertureEngine.h)
    // and the test block size, or the overlap window below would be
    // meaningless.
    REQUIRE (latency < testBlockSize / 2);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto overlapLength = testBlockSize - latency;
    REQUIRE (overlapLength > testBlockSize / 2);

    // < -90 dBFS residual, in linear amplitude.
    constexpr float tolerance = 3.1623e-5f; // 10^(-90/20)

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

TEST_CASE ("Engine sanity test: minimum drive keeps the wet path near-linear", "[dsp][engine]")
{
    OvertureEngine engine;

    // Minimum drive (0 dB), Tight/Tone set well outside the test tone's
    // passband edge so they contribute negligible magnitude/phase change at
    // 1 kHz, Mix fully wet so we are measuring the wet chain itself.
    engine.setDriveDb (0.0f);
    engine.setTightFrequencyHz (20.0f);
    engine.setToneFrequencyHz (8000.0f);
    engine.setLevelDb (0.0f);
    engine.setMixProportion (1.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    const auto latency = engine.getLatencySamples();
    REQUIRE (latency < testBlockSize / 2);

    // Low amplitude (-34 dBFS): comfortably inside the clipper's near-linear
    // region even with zero headroom above unity drive gain. This is the
    // correct way to probe "is the wet path linear at minimum drive" - the
    // clipper is a tanh curve, so it is never perfectly linear for any
    // amplitude, but it approaches linearity as amplitude shrinks.
    juce::AudioBuffer<float> warmup (2, testBlockSize);
    TestHelpers::fillWithSine (warmup, testSampleRate, testFrequencyHz, 0.02f, 0);

    // Run one full block through first purely to let the Tight HPF (a 2nd-
    // order filter with a very low, 20 Hz cutoff here) settle out of its
    // zero-state turn-on transient. That transient is real and expected
    // filter behaviour, not evidence of clipper nonlinearity - measuring
    // "near-linear" has to look at the settled steady state, the same way
    // GainProcessingTests.cpp's settleSmoothing() lets parameter ramps
    // settle before measuring. The measurement block's sine continues the
    // warm-up block's phase (startSampleIndex = testBlockSize) so there is
    // no artificial phase discontinuity re-exciting the filter's state
    // right at the start of the measured window.
    {
        juce::dsp::AudioBlock<float> warmupBlock (warmup);
        engine.process (warmupBlock);
    }

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.02f, testBlockSize);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto overlapLength = testBlockSize - latency;
    REQUIRE (overlapLength > testBlockSize / 2);

    // Search a small window of sample-alignment offsets around the reported
    // oversampling latency: the Tight/Tone IIR filters have their own tiny
    // (sub-block, sub-10-sample) group delay at 1 kHz which is not part of
    // getLatencySamples() and is not what this test is probing. Using the
    // best alignment in that narrow window isolates genuine clipper/filter
    // shape nonlinearity from that legitimate, unreported group delay.
    constexpr int maxUnaccountedGroupDelaySamples = 8;

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto correlation = TestHelpers::bestCorrelationOverShift (
            processed.getReadPointer (channel) + latency,
            reference.getReadPointer (channel),
            overlapLength,
            maxUnaccountedGroupDelaySamples);

        CHECK (correlation > 0.9999);
    }
}

TEST_CASE ("Engine reset() clears filter/oversampler/delay state without crashing", "[dsp][engine]")
{
    OvertureEngine engine;
    engine.setDriveDb (30.0f);
    engine.setMixProportion (1.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK_NOTHROW (engine.reset());
    CHECK (TestHelpers::allSamplesFinite (buffer));

    // Processing again straight after reset() must not crash or produce
    // non-finite output.
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);
    CHECK_NOTHROW (engine.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}
