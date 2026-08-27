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
    setParam (processor, ParamIDs::biteTilt, 100.0f);
    setParam (processor, ParamIDs::level, 24.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    // Sane bound, not just "finite" - loosened from v0.1's 100.0f (adapted,
    // and by a lot): Bite Tilt at +100% is a new, deliberately generous
    // post-clip *boost* stage reaching a very large asymptotic gain well
    // above its ~3 kHz corner (docs/design-brief.md's Bite Tilt section;
    // v0.1's Tone could only ever cut, never boost) stacked on top of
    // already-maximal Drive/Tight/Level - a combination with no realistic
    // musical use but that this test intentionally still exercises for "no
    // NaN/Inf/runaway instability", not a tight numeric ceiling; see the
    // dedicated Bite Tilt bidirectionality tests in tests/EngineTests.cpp
    // for precisely-bounded gain assertions instead.
    CHECK (TestHelpers::peakAbsolute (buffer) < 1.0e7f);
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
        setParam (processor, ParamIDs::biteTilt, useMinimum ? -100.0f : 100.0f);
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
        setParam (processor, ParamIDs::biteTilt, -100.0f + unit (rng) * 200.0f);
        setParam (processor, ParamIDs::level, -24.0f + unit (rng) * 48.0f);
        setParam (processor, ParamIDs::mix, unit (rng) * 100.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

//==============================================================================
// v0.2.0 "NaN/Inf robustness on all new controls" guarantee
// (docs/design-brief.md guarantee 7): sweeps biteAmount, kneeSoften,
// asymmetryAmount, biteTilt to their extremes combined with extreme
// Drive/Tight/Level and confirms no NaN/Inf propagates through the engine -
// carries forward the pre-existing "Extreme parameter values at both range
// edges" test above to the four new controls specifically.
TEST_CASE ("Extreme new-control (Bite/Knee Soften/Asymmetry/Bite Tilt) values combined with extreme "
           "Drive/Tight/Level produce no NaN/Inf",
           "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        for (auto voicingIndex : { 0.0f, 1.0f, 2.0f })
        {
            setParam (processor, ParamIDs::tight, useMinimum ? 20.0f : 400.0f);
            setParam (processor, ParamIDs::drive, useMinimum ? 0.0f : 40.0f);
            setParam (processor, ParamIDs::biteAmount, useMinimum ? 0.0f : 100.0f);
            setParam (processor, ParamIDs::kneeSoften, useMinimum ? 0.0f : 100.0f);
            setParam (processor, ParamIDs::asymmetryAmount, useMinimum ? 0.0f : 100.0f);
            setParam (processor, ParamIDs::biteTilt, useMinimum ? -100.0f : 100.0f);
            setParam (processor, ParamIDs::level, useMinimum ? -24.0f : 24.0f);
            setParam (processor, ParamIDs::mix, useMinimum ? 0.0f : 100.0f);
            setParam (processor, ParamIDs::voicing, voicingIndex);

            TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 1.0f);

            CHECK_NOTHROW (processor.processBlock (buffer, midi));
            CHECK (TestHelpers::allSamplesFinite (buffer));
        }
    }
}

TEST_CASE ("Rapid automation of Bite/Knee Soften/Asymmetry/Bite Tilt across many blocks produces no NaN/Inf",
           "[robustness]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (5678);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::drive, unit (rng) * 40.0f);
        setParam (processor, ParamIDs::biteAmount, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::kneeSoften, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::asymmetryAmount, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::biteTilt, -100.0f + unit (rng) * 200.0f);

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
    setParam (processor, ParamIDs::biteAmount, 70.0f);
    setParam (processor, ParamIDs::kneeSoften, 60.0f);
    setParam (processor, ParamIDs::biteTilt, -30.0f);
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

// =========================================================================
// Fleet audit class 2b (issue #35): the decaying-tail denormal guard.
//
// The fleet ships with JUCE_DSP_ENABLE_SNAP_TO_ZERO=0 and relies wholly on
// the juce::ScopedNoDenormals held across processBlock() for its denormal
// discipline. This test proves that reliance for Overture's recursive
// state: the Tight HPF, the oversampler's own filter tails, the Feedback
// voicing's feedback clipper state, the Enhanced-path 5 Hz DC blocker
// (double state), the Bite/Bite Tilt filters and the gate's mean-square
// envelope. A loud burst charges everything, then digital silence must
// leave
//   (a) no subnormal output samples (FP_ZERO/FP_NORMAL only) - guaranteed
//       while the ScopedNoDenormals holds, tripped on every platform the
//       moment anyone drops it or adds a path outside its scope;
//   (b) an exact-zero rest - a state parked on a small-but-normal rounding
//       fixed point (the Miserere#46 failure class, ~1e-34 on x86,
//       sustained indefinitely) fails this even with FTZ on;
//   (c) silent blocks no dearer than busy blocks - the audible symptom of
//       denormal grinding on Intel is a plugin that slows down exactly
//       when the track goes quiet. Both timed loops share one body, so the
//       ratio isolates DSP cost. The 10x bound derives from the failure
//       mode (Intel's subnormal microcode assist costs 10-100x per op) and
//       not from noise: scheduler jitter on a mean over ~840 blocks stays
//       within a few tens of percent.
//
// Two sections: with the gate off the resting output is unmasked (a parked
// state anywhere upstream shows directly); with the gate on, the gate's
// own envelope/hold states join the test and the CPU/subnormal checks keep
// guarding what the gate's attenuation might visually hide.
TEST_CASE ("After a loud burst, a silent tail decays to exact-zero rest with no denormal residue",
           "[robustness][denormal]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    OvertureAudioProcessor processor;

    // Worst-case sustained-state settings: Feedback voicing (the one
    // genuinely recursive clipper topology), Enhanced quality (engages the
    // DC blocker), 4x oversampling, hot drive, Tight HPF up, tilt fully
    // dark, mix fully wet.
    setParam (processor, ParamIDs::drive, 40.0f);
    setParam (processor, ParamIDs::mix, 100.0f);
    setParam (processor, ParamIDs::voicing, 3.0f);      // Feedback
    setParam (processor, ParamIDs::clipQuality, 1.0f);  // Enhanced
    setParam (processor, ParamIDs::oversampling, 1.0f); // 4x
    setParam (processor, ParamIDs::tight, 120.0f);
    setParam (processor, ParamIDs::biteTilt, -100.0f);
    setParam (processor, ParamIDs::asymmetryAmount, 100.0f);

    SECTION ("gate off: the resting output is unmasked")
    {
        setParam (processor, ParamIDs::gate, 0.0f);
    }

    SECTION ("gate on: the gate's own envelope joins the tail")
    {
        setParam (processor, ParamIDs::gate, 1.0f);
        setParam (processor, ParamIDs::gateThreshold, -50.0f);
    }

    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    constexpr auto blocksPerSecond = static_cast<int> (sampleRate) / blockSize;

    // Two seconds of hot programme, timing the second second with the same
    // loop body as the silent loop below.
    for (int block = 0; block < blocksPerSecond; ++block)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 110.0, 0.9f,
                                   static_cast<juce::int64> (block) * blockSize);
        processor.processBlock (buffer, midi);
    }

    const auto busyStart = juce::Time::getHighResolutionTicks();

    for (int block = 0; block < blocksPerSecond; ++block)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 110.0, 0.9f,
                                   static_cast<juce::int64> (block) * blockSize);
        processor.processBlock (buffer, midi);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
    }

    const auto busyTicks = juce::Time::getHighResolutionTicks() - busyStart;

    // Ten seconds of digital silence. The first two seconds legitimately
    // carry the oversampler/filter ring-out, the gate's release and the
    // engine's one-second rest-flush dwell (see OvertureEngine.h); after
    // that, any surviving non-zero sample is a parked state.
    constexpr int silentBlocks = 10 * blocksPerSecond;

    float worstTail = 0.0f;
    int subnormalSamples = 0;

    const auto silentStart = juce::Time::getHighResolutionTicks();

    for (int block = 0; block < silentBlocks; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));

        // The subnormal census runs from the FIRST silent block: with
        // FTZ/DAZ engaged no output sample can ever classify as subnormal,
        // while without it the decaying tail itself sweeps through the
        // subnormal range on its way down and trips this on every
        // platform. worstTail, by contrast, only counts after the
        // legitimate ring-out window.
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto classification = std::fpclassify (data[sample]);

                if (classification != FP_ZERO && classification != FP_NORMAL)
                    ++subnormalSamples;
            }
        }

        if (block < 2 * blocksPerSecond)
            continue;

        worstTail = juce::jmax (worstTail, TestHelpers::peakAbsolute (buffer));
    }

    const auto silentTicks = juce::Time::getHighResolutionTicks() - silentStart;

    INFO ("worst tail after 2 s of silence = " << worstTail
          << ", subnormal samples = " << subnormalSamples);

    // (a) FTZ discipline covers the whole output path.
    CHECK (subnormalSamples == 0);

    // (b) True rest - measured at exactly zero on both arm64 (native) and
    // x86_64 (Rosetta 2, SSE mul/add rounding like the Windows leg).
    CHECK (worstTail == 0.0f);

    // (c) Silence must not cost more than programme (derivation above).
    const auto busyPerBlock = static_cast<double> (busyTicks) / blocksPerSecond;
    const auto silentPerBlock = static_cast<double> (silentTicks) / silentBlocks;

    INFO ("silent block cost " << silentPerBlock << " ticks vs busy " << busyPerBlock);
    CHECK (silentPerBlock <= busyPerBlock * 10.0);

    // And the engine wakes up cleanly (a few blocks: the oversampling
    // latency and, in the gated section, the gate's attack need a moment
    // before output is fully back).
    for (int block = 0; block < 8; ++block)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 110.0, 0.9f,
                                   static_cast<juce::int64> (block) * blockSize);
        processor.processBlock (buffer, midi);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
    }

    CHECK (TestHelpers::peakAbsolute (buffer) > 1.0e-3f);
}
