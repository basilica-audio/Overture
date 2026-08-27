#include "dsp/OvertureEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

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

    // Parameters other than Mix are deliberately set to non-neutral values,
    // including every v0.2.0 control - a true null test has to prove the
    // *entire* wet chain is bypassed, not just that it happens to be quiet
    // at default settings.
    engine.setMixProportion (0.0f);
    engine.setDriveDb (25.0f);
    engine.setTightFrequencyHz (300.0f);
    engine.setBiteAmountPercent (80.0f);
    engine.setKneeSoftenPercent (70.0f);
    engine.setAsymmetryAmountPercent (90.0f);
    engine.setBiteTiltPercent (-40.0f);
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

    // Minimum drive (0 dB), Tight set well below and Bite Tilt left flat
    // (0% - a true no-op, see OvertureEngine.cpp) so neither contributes any
    // magnitude/phase change at 1 kHz, Bite/Knee Soften disabled so the
    // clipper's own frequency-independent near-linear region is what's
    // being measured, Mix fully wet so we are measuring the wet chain
    // itself.
    engine.setDriveDb (0.0f);
    engine.setTightFrequencyHz (20.0f);
    engine.setBiteAmountPercent (0.0f);
    engine.setKneeSoftenPercent (0.0f);
    engine.setBiteTiltPercent (0.0f);
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
    // oversampling latency: the Tight IIR filter has its own tiny (sub-
    // block, sub-10-sample) group delay at 1 kHz which is not part of
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

        // v0.2.0 threshold note (adapted from v0.1's 0.9999): with Bite
        // Tilt at 0% now a *true* skip (no filter call at all - see
        // OvertureEngine.cpp), the clipper's own tiny residual harmonic
        // content at this amplitude is no longer incidentally scrubbed by
        // v0.1's always-active (even "wide open") Tone low-pass, which
        // very slightly reduces the correlation-with-a-pure-sine metric
        // versus v0.1. 0.999 is still an extremely tight near-linearity
        // bound (v0.1's own passband sanity check elsewhere in this file
        // used a much looser +/-1 dB).
        CHECK (correlation > 0.999);
    }
}

//==============================================================================
// v0.2.0 Bite Tilt tests (replaces v0.1's cut-only Tone LPF - see
// docs/design-brief.md's "Bite" section and guarantee 5).
namespace
{
    // Runs a fixed-frequency sine through an engine configured with a given
    // biteTilt setting (Drive at 0 dB / low amplitude keeps the clipper in
    // its near-linear region, matching the technique the near-linear test
    // above uses) and returns the measured output RMS attenuation (dB)
    // relative to the input.
    double measureBiteTiltAttenuationDb (float biteTiltPercent, double frequencyHz)
    {
        OvertureEngine engine;
        engine.setDriveDb (0.0f);
        engine.setTightFrequencyHz (20.0f);
        engine.setBiteAmountPercent (0.0f);
        engine.setKneeSoftenPercent (0.0f);
        // Soft Symmetric (unbiased tanh, a=0) rather than the Asymmetric
        // default: the Asymmetric voicing's own small-signal gain is
        // sech^2(a) < 1 by design (see AsymSoftClipperTests.cpp's
        // near-linear test), which would otherwise show up here as a
        // constant ~0.3 dB "attenuation" unrelated to Bite Tilt - isolating
        // Bite Tilt's own contribution needs a clipper stage that is
        // genuinely unity-gain at this tiny test amplitude.
        engine.setClipperVoicing (ClipperVoicing::softSymmetric);
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setBiteTiltPercent (biteTiltPercent);

        const auto spec = makeTestSpec (2);
        engine.prepare (spec);

        constexpr float testAmplitude = 0.1f;

        juce::AudioBuffer<float> warmup (2, testBlockSize);
        TestHelpers::fillWithSine (warmup, testSampleRate, frequencyHz, testAmplitude, 0);
        {
            juce::dsp::AudioBlock<float> warmupBlock (warmup);
            engine.process (warmupBlock);
        }

        juce::AudioBuffer<float> measured (2, testBlockSize);
        TestHelpers::fillWithSine (measured, testSampleRate, frequencyHz, testAmplitude, testBlockSize);

        juce::dsp::AudioBlock<float> measuredBlock (measured);
        engine.process (measuredBlock);

        const auto inputRms = static_cast<double> (testAmplitude) / std::sqrt (2.0);
        const auto outputRms = TestHelpers::rms (measured);
        return 20.0 * std::log10 (outputRms / inputRms);
    }
}

TEST_CASE ("Bite Tilt: flat (0%) is a true no-op relative to an unfiltered signal", "[dsp][engine][bitetilt]")
{
    // "True no-op" here means the filter is skipped entirely (see
    // OvertureEngine.cpp's `if (biteTiltPercent != 0.0f)` gate) - well
    // inside the shelf's own passband-side asymptote, attenuation must be
    // essentially 0 dB regardless of which side of the 3 kHz corner the
    // test frequency sits on.
    for (double frequencyHz : { 200.0, 1000.0, 6000.0, 12000.0 })
        CHECK (measureBiteTiltAttenuationDb (0.0f, frequencyHz) == Catch::Approx (0.0).margin (0.2));
}

TEST_CASE ("Bite Tilt: negative values darken high-frequency content monotonically", "[dsp][engine][bitetilt]")
{
    constexpr double aboveCornerHz = 8000.0; // well above the ~3 kHz corner

    double previousAttenuationDb = 1.0; // any value > 0 dB, the loop's first iteration always improves on it
    bool first = true;

    for (float biteTiltPercent : { 0.0f, -25.0f, -50.0f, -75.0f, -100.0f })
    {
        const auto attenuationDb = measureBiteTiltAttenuationDb (biteTiltPercent, aboveCornerHz);

        if (! first)
            CHECK (attenuationDb <= previousAttenuationDb + 0.05); // monotonically non-increasing (getting darker)

        previousAttenuationDb = attenuationDb;
        first = false;
    }

    CHECK (previousAttenuationDb < -10.0); // -100% is genuinely, audibly dark well above the corner
}

TEST_CASE ("Bite Tilt: positive values brighten content above the corner relative to flat", "[dsp][engine][bitetilt]")
{
    constexpr double aboveCornerHz = 8000.0;

    const auto flatDb = measureBiteTiltAttenuationDb (0.0f, aboveCornerHz);
    const auto boostedDb = measureBiteTiltAttenuationDb (100.0f, aboveCornerHz);

    CHECK (boostedDb > flatDb + 1.0); // measurable boost above flat, not just noise
}

TEST_CASE ("Bite Tilt: the boost direction is bounded at +12 dB, so a positive tilt cannot run away "
           "with the output level",
           "[dsp][engine][bitetilt][headroom]")
{
    // The defect this pins (issue #44). Bite Tilt used ONE symmetric ceiling
    // for both directions, and that ceiling was the 100 dB the CUT direction
    // needs for its v0.1 Tone backward-compatibility guarantee (see the test
    // below, and biteTiltMaxCutDb in OvertureEngine.h). A positive tilt
    // therefore applied very nearly one dB of broadband boost per percent of
    // knob travel: the Fuzz-Adjacent Lead factory preset, whose Hard Clip
    // voicing bounds its pre-tilt signal at 0 dBFS BY CONSTRUCTION, rendered
    // the reference programme at +27.10 dBFS - of which +23.27 dB was this
    // shelf at a Bite Tilt of just +25%. Nothing in docs/design-brief.md ever
    // asked the boost direction for more than "brighter".
    //
    // The bound is derived rather than tasted. biteTiltMaxBoostDb is the
    // shelf's ASYMPTOTIC gain far above the corner, and the shelf is a
    // 2nd-order RBJ section at Q = 1/sqrt(2) (OvertureEngine::shelfQ) -
    // Butterworth, so its magnitude approaches that asymptote monotonically
    // from below and never peaks above it. The realised boost is therefore
    // bounded by the nominal ceiling at EVERY frequency, at every setting,
    // which is what the sweep below asserts.
    constexpr double ceilingDb = 12.0; // OvertureEngine::biteTiltMaxBoostDb
    constexpr double numericToleranceDb = 0.1;

    const auto boostAt = [] (float tiltPercent, double frequencyHz)
    {
        return measureBiteTiltAttenuationDb (tiltPercent, frequencyHz)
                - measureBiteTiltAttenuationDb (0.0f, frequencyHz);
    };

    for (const float tiltPercent : { 25.0f, 50.0f, 100.0f })
        for (const double frequencyHz : { 100.0, 500.0, 1000.0, 3000.0, 6000.0, 12000.0 })
        {
            INFO ("Bite Tilt +" << tiltPercent << "% at " << frequencyHz << " Hz");
            CHECK (boostAt (tiltPercent, frequencyHz) <= ceilingDb + numericToleranceDb);
        }

    // ...and the ceiling is genuinely reached well above the corner, so the
    // bound above is tight rather than vacuously true of any small number.
    const auto asymptoticBoostDb = boostAt (100.0f, 12000.0);
    INFO ("asymptotic boost at +100%: " << asymptoticBoostDb << " dB");
    CHECK (asymptoticBoostDb > ceilingDb - 1.0);

    // The knob is still monotonic in the boost direction - capping the
    // ceiling must not have flattened the upper half into a plateau.
    CHECK (boostAt (100.0f, 12000.0) > boostAt (50.0f, 12000.0) + 1.0);
    CHECK (boostAt (50.0f, 12000.0) > boostAt (25.0f, 12000.0) + 1.0);
}

TEST_CASE ("Bite Tilt: fully-negative (-100%) subsumes v0.1's entire cut-only Tone range - "
           "at least as dark as v0.1's fully-closed Tone at the same test frequency",
           "[dsp][engine][bitetilt][backcompat]")
{
    // v0.1's Tone was a 4th-order Butterworth low-pass, built as two
    // cascaded 2nd-order sections sharing a cutoff (see the retired
    // toneFilterQ1/Q2 constants this reconstructs directly via JUCE's
    // allocating - fine here, this is test setup, not audio-thread code -
    // IIR::Coefficients::makeLowPass, matching OvertureEngine.cpp's v0.1
    // priming exactly). "Fully-closed" is the v0.1 Tone parameter's range
    // minimum, 1000 Hz. This test computes that legacy filter's own
    // magnitude response directly (juce::dsp::IIR::Coefficients::
    // getMagnitudeForFrequency(), JUCE 8.0.14) rather than hardcoding a
    // specific dB figure, so it stays correct even if the exact
    // biteTiltMaxDb constant is ever retuned.
    constexpr double legacyToneCutoffHz = 1000.0;
    constexpr double testFrequencyForComparisonHz = 4000.0; // 2 octaves above the legacy cutoff, matching v0.1's own tone-stack test convention
    constexpr float legacyToneFilterQ1 = 0.5411961f;
    constexpr float legacyToneFilterQ2 = 1.3065630f;

    const auto stage1 = juce::dsp::IIR::Coefficients<float>::makeLowPass (testSampleRate, legacyToneCutoffHz, legacyToneFilterQ1);
    const auto stage2 = juce::dsp::IIR::Coefficients<float>::makeLowPass (testSampleRate, legacyToneCutoffHz, legacyToneFilterQ2);

    const auto legacyMagnitude = stage1->getMagnitudeForFrequency (testFrequencyForComparisonHz, testSampleRate)
                                  * stage2->getMagnitudeForFrequency (testFrequencyForComparisonHz, testSampleRate);
    const auto legacyAttenuationDb = 20.0 * std::log10 (legacyMagnitude);

    const auto newAttenuationDb = measureBiteTiltAttenuationDb (-100.0f, testFrequencyForComparisonHz);

    CHECK (newAttenuationDb <= legacyAttenuationDb);
}

TEST_CASE ("Engine clipper voicing: switching voicing changes the output for a hot input", "[dsp][engine][voicing]")
{
    const auto runWithVoicing = [&] (ClipperVoicing voicing)
    {
        OvertureEngine engine;
        engine.setDriveDb (20.0f);
        engine.setTightFrequencyHz (20.0f);
        engine.setBiteAmountPercent (0.0f);
        engine.setKneeSoftenPercent (0.0f);
        engine.setBiteTiltPercent (0.0f);
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setClipperVoicing (voicing);

        const auto spec = makeTestSpec (2);
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.8f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        CHECK (TestHelpers::allSamplesFinite (buffer));
        return TestHelpers::rms (buffer);
    };

    const auto asymmetricRms = runWithVoicing (ClipperVoicing::asymmetric);
    const auto softSymmetricRms = runWithVoicing (ClipperVoicing::softSymmetric);
    const auto hardClipRms = runWithVoicing (ClipperVoicing::hardClip);

    // Different clipping curves at a hot drive level must produce
    // measurably different RMS levels - otherwise the Voicing parameter
    // would have no audible effect, defeating its purpose.
    CHECK (asymmetricRms != Catch::Approx (softSymmetricRms).margin (1.0e-4));
    CHECK (asymmetricRms != Catch::Approx (hardClipRms).margin (1.0e-4));
    CHECK (softSymmetricRms != Catch::Approx (hardClipRms).margin (1.0e-4));
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

TEST_CASE ("Engine defensively chunks a block larger than the size declared to prepare()", "[dsp][engine][robustness]")
{
    // basilica-audio/Overture issue #13: prepare()/process() had no clamp
    // against a host handing process() more samples than the
    // spec.maximumBlockSize it declared to prepare() - the oversampler's
    // and DryWetMixer's internal buffers are both fixed to that size and
    // only guarded by a jassert, which compiles out in Release builds (see
    // JUCE 8.0.14 juce_Oversampling.cpp's processSamplesUp and
    // juce_DryWetMixer.cpp's pushDrySamples). A single oversized process()
    // call used to hand that whole block straight to both, overrunning
    // their internal buffers.
    //
    // This proves more than "doesn't crash": it proves the internal
    // chunking added to fix #13 produces *bit-identical* output to what a
    // well-behaved host would get by calling process() once per
    // preparedSize-sample chunk itself - i.e. the defensive chunk boundary
    // reproduces exactly the same per-chunk coefficient/Mix smoothing
    // cadence a correctly-behaving host would drive. Exercises every
    // v0.2.0 control (Bite/Knee Soften/Asymmetry/Bite Tilt) at non-zero
    // values so their new coefficient-recompute/skip-gate code paths are
    // covered by the chunk-boundary comparison too, not just the
    // pre-existing Tight/Drive/Level/Mix chain.
    constexpr int preparedSize = 128;
    constexpr int oversizedBlockSamples = 8192; // 64x the declared block size

    const auto makeConfiguredEngine = [] () -> std::unique_ptr<OvertureEngine>
    {
        auto engine = std::make_unique<OvertureEngine>();
        engine->setDriveDb (20.0f);
        engine->setTightFrequencyHz (150.0f);
        engine->setBiteAmountPercent (50.0f);
        engine->setKneeSoftenPercent (50.0f);
        engine->setAsymmetryAmountPercent (60.0f);
        engine->setBiteTiltPercent (-20.0f);
        engine->setLevelDb (6.0f);
        engine->setMixProportion (1.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (preparedSize);
        spec.numChannels = 2;
        engine->prepare (spec);
        return engine;
    };

    juce::AudioBuffer<float> input (2, oversizedBlockSamples);
    TestHelpers::fillWithSine (input, testSampleRate, testFrequencyHz, 0.6f);

    // Reference: process the exact same signal the way a well-behaved host
    // would - one process() call per preparedSize-sample chunk.
    const auto referenceEngine = makeConfiguredEngine();
    juce::AudioBuffer<float> referenceOutput;
    referenceOutput.makeCopyOf (input);

    for (int offset = 0; offset < oversizedBlockSamples; offset += preparedSize)
    {
        const auto chunkLength = juce::jmin (preparedSize, oversizedBlockSamples - offset);
        juce::AudioBuffer<float> chunkView (referenceOutput.getArrayOfWritePointers(), 2, offset, chunkLength);
        juce::dsp::AudioBlock<float> chunkBlock (chunkView);
        referenceEngine->process (chunkBlock);
    }

    // Under test: a single process() call carrying the whole oversized
    // block at once - the exact failure mode issue #13 was filed against.
    const auto oversizedEngine = makeConfiguredEngine();
    juce::AudioBuffer<float> oversizedOutput;
    oversizedOutput.makeCopyOf (input);

    juce::dsp::AudioBlock<float> oversizedBlock (oversizedOutput);
    CHECK_NOTHROW (oversizedEngine->process (oversizedBlock));

    CHECK (TestHelpers::allSamplesFinite (oversizedOutput));

    for (int channel = 0; channel < oversizedOutput.getNumChannels(); ++channel)
    {
        const auto* refData = referenceOutput.getReadPointer (channel);
        const auto* outData = oversizedOutput.getReadPointer (channel);

        for (int sample = 0; sample < oversizedBlockSamples; ++sample)
            CHECK (outData[sample] == Catch::Approx (refData[sample]).margin (1.0e-6));
    }
}

//==============================================================================
// v0.2.0 "bite_amount" guarantee (docs/design-brief.md guarantee 2):
// frequency-dependent gain proof. A low-frequency sine (80 Hz, below the
// ~700 Hz Bite shelf corner) and a higher-frequency sine (2 kHz, above it)
// at matched input level and a fixed Drive, with biteAmount swept 0->100%;
// the clip-onset (peak) level gap between the two must increase
// monotonically with biteAmount - bass is progressively clipped less than
// treble, not just filtered out beforehand (the mechanism, not merely the
// result - see docs/design-brief.md SS1/SS"bite_amount").
TEST_CASE ("Bite: low/high-frequency clip-onset peak gap grows monotonically with biteAmount at a fixed Drive",
           "[dsp][engine][bite]")
{
    constexpr double lowFrequencyHz = 80.0;
    constexpr double highFrequencyHz = 2000.0;
    constexpr float testAmplitude = 0.5f;
    constexpr float fixedDriveDb = 10.0f; // moderate - saturates but leaves room for the shelf's effect to show

    const auto measurePeakAt = [&] (double frequencyHz, float biteAmountPercent)
    {
        OvertureEngine engine;
        engine.setTightFrequencyHz (20.0f); // well below both test tones - negligible attenuation of either
        engine.setDriveDb (fixedDriveDb);
        engine.setBiteAmountPercent (biteAmountPercent);
        engine.setKneeSoftenPercent (0.0f); // isolate Bite from Knee Soften's own level-shaping
        engine.setAsymmetryAmountPercent (40.0f); // v0.1's default bias
        engine.setBiteTiltPercent (0.0f); // flat/no-op post-clip stage
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setClipperVoicing (ClipperVoicing::asymmetric);

        const auto spec = makeTestSpec (2);
        engine.prepare (spec);

        juce::AudioBuffer<float> warmup (2, testBlockSize);
        TestHelpers::fillWithSine (warmup, testSampleRate, frequencyHz, testAmplitude, 0);
        {
            juce::dsp::AudioBlock<float> warmupBlock (warmup);
            engine.process (warmupBlock);
        }

        juce::AudioBuffer<float> measured (2, testBlockSize);
        TestHelpers::fillWithSine (measured, testSampleRate, frequencyHz, testAmplitude, testBlockSize);
        juce::dsp::AudioBlock<float> measuredBlock (measured);
        engine.process (measuredBlock);

        CHECK (TestHelpers::allSamplesFinite (measured));
        return TestHelpers::peakAbsolute (measured);
    };

    float previousGapDb = -1.0e9f;

    for (float biteAmountPercent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const auto lowPeak = measurePeakAt (lowFrequencyHz, biteAmountPercent);
        const auto highPeak = measurePeakAt (highFrequencyHz, biteAmountPercent);

        REQUIRE (lowPeak > 0.0f);
        REQUIRE (highPeak > 0.0f);

        const auto gapDb = 20.0f * std::log10 (highPeak / lowPeak);

        CHECK (gapDb >= previousGapDb - 0.05f); // monotonically non-decreasing (small float-noise tolerance)
        previousGapDb = gapDb;
    }

    // 0% and 100% must genuinely differ - otherwise biteAmount would have no
    // audible frequency-selective effect at all.
    CHECK (previousGapDb > 1.0f);
}

//==============================================================================
// v0.2.0 "knee_soften" guarantee (docs/design-brief.md guarantee 3):
// drive-dependent knee softening proof. At a fixed kneeSoften > 0, the
// softening effect (measured here as the crest factor - peak/RMS - lift
// relative to the kneeSoften = 0 baseline AT THE SAME Drive, which isolates
// the knee-softening-specific contribution from the ordinary "more Drive
// alone makes a clipped wave more square" effect that changes crest factor
// regardless of Knee Soften) must be measurably larger at high Drive than
// at low Drive. Hard Clip is used because it has literally zero knee at
// kneeSoften = 0 (a razor-sharp clamp) at any Drive level, making the
// softening effect maximally visible/unambiguous - see
// docs/design-brief.md's explicit callout of Hard Clip for this guarantee.
namespace
{
    juce::AudioBuffer<float> renderHardClip (float driveDb, float kneeSoftenPercent)
    {
        OvertureEngine engine;
        engine.setTightFrequencyHz (20.0f);
        engine.setDriveDb (driveDb);
        engine.setBiteAmountPercent (0.0f);
        engine.setKneeSoftenPercent (kneeSoftenPercent);
        engine.setBiteTiltPercent (0.0f);
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setClipperVoicing (ClipperVoicing::hardClip);

        const auto spec = makeTestSpec (2);
        engine.prepare (spec);

        constexpr float testAmplitude = 0.5f;

        juce::AudioBuffer<float> warmup (2, testBlockSize);
        TestHelpers::fillWithSine (warmup, testSampleRate, testFrequencyHz, testAmplitude, 0);
        {
            juce::dsp::AudioBlock<float> warmupBlock (warmup);
            engine.process (warmupBlock);
        }

        juce::AudioBuffer<float> measured (2, testBlockSize);
        TestHelpers::fillWithSine (measured, testSampleRate, testFrequencyHz, testAmplitude, testBlockSize);
        juce::dsp::AudioBlock<float> measuredBlock (measured);
        engine.process (measuredBlock);
        return measured;
    }

    // RMS of the sample-by-sample difference between a softened
    // (kneeSoften = fixedKneeSoftenPercent) and sharp (kneeSoften = 0)
    // render at the same Drive - a direct, THD-like measure of how much
    // Knee Soften actually changes the waveform shape at that Drive level,
    // free of the "just more Drive alone makes Hard Clip more square-ish"
    // confound a bulk crest-factor (peak/RMS) comparison has: uniformly
    // rescaling an already near-square wave's flat-top level (which is
    // what Knee Soften does across almost the entire cycle once deeply
    // saturated) barely moves peak/RMS, even though the actual sample-level
    // waveform, and therefore its audible harmonic content, has measurably
    // changed. Both renders share identical latency (Knee Soften doesn't
    // touch the oversampler), so they're already sample-aligned - no
    // shift-search needed.
    float measureSofteningDifferenceRms (float driveDb, float fixedKneeSoftenPercent)
    {
        const auto sharp = renderHardClip (driveDb, 0.0f);
        const auto soft = renderHardClip (driveDb, fixedKneeSoftenPercent);

        juce::AudioBuffer<float> difference;
        difference.makeCopyOf (soft);

        for (int channel = 0; channel < difference.getNumChannels(); ++channel)
            juce::FloatVectorOperations::subtract (difference.getWritePointer (channel), sharp.getReadPointer (channel), difference.getNumSamples());

        return static_cast<float> (TestHelpers::rms (difference));
    }
}

TEST_CASE ("Knee Soften: waveform-shape change from softening is measurably larger at high Drive than at low Drive",
           "[dsp][engine][knee]")
{
    constexpr float lowDriveDb = 3.0f;
    constexpr float highDriveDb = 30.0f;
    constexpr float fixedKneeSoftenPercent = 80.0f;

    const auto differenceAtLowDrive = measureSofteningDifferenceRms (lowDriveDb, fixedKneeSoftenPercent);
    const auto differenceAtHighDrive = measureSofteningDifferenceRms (highDriveDb, fixedKneeSoftenPercent);

    CHECK (differenceAtHighDrive > differenceAtLowDrive);
    CHECK (differenceAtHighDrive > 0.01f); // genuinely audible, not just float noise
}

TEST_CASE ("Knee Soften: at kneeSoften=0, high and low Drive renders are bit-identical to their own "
           "kneeSoften=0 selves (the drive-invariance half of the guarantee)",
           "[dsp][engine][knee]")
{
    // The formula OvertureEngine::processChunk() uses
    // (kneeBlend01 = (kneeSoftenPercent * 0.01f) * driveIntensity01) is
    // algebraically always exactly 0 when kneeSoftenPercent == 0, regardless
    // of driveIntensity01/Drive - already exercised directly (without an
    // engine) by KneeSofteningTests.cpp's identity-function test. This is
    // the engine-level companion: at kneeSoften = 0, measureSofteningDifferenceRms()
    // itself (softened vs sharp render at the SAME Drive) must be exactly
    // zero at both a low and a high Drive - i.e. nothing in the surrounding
    // chain introduces a spurious Drive-dependent "softening" side effect
    // when the control itself is off.
    CHECK (measureSofteningDifferenceRms (3.0f, 0.0f) == Catch::Approx (0.0f).margin (1.0e-7));
    CHECK (measureSofteningDifferenceRms (30.0f, 0.0f) == Catch::Approx (0.0f).margin (1.0e-7));
}

//==============================================================================
// v0.3.0 engine tests (brief SS6: T-S2's engine-level companion, T-E1, T-E2,
// T-E3).
namespace v030
{
    // FNV-1a over the raw float bits of a buffer - an exact bit-identity
    // fingerprint, deliberately not a tolerance.
    inline juce::uint64 hashBuffer (const juce::AudioBuffer<float>& buffer, juce::uint64 seed) noexcept
    {
        auto hash = seed;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                juce::uint32 bits;
                const auto value = data[sample];
                std::memcpy (&bits, &value, sizeof (bits));

                for (int byte = 0; byte < 4; ++byte)
                {
                    hash ^= static_cast<juce::uint64> ((bits >> (8 * byte)) & 0xffu);
                    hash *= 1099511628211ULL;
                }
            }
        }

        return hash;
    }

    // The exact fixture the v0.2.0 goldens below were recorded with.
    inline juce::uint64 runGoldenFixture (int voicingIndex, int oversamplingPow2)
    {
        OvertureEngine engine;
        engine.setOversamplingFactorPow2 (oversamplingPow2);

        juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        engine.prepare (spec);

        engine.setClipperVoicing (static_cast<ClipperVoicing> (voicingIndex));
        engine.setTightFrequencyHz (100.0f);
        engine.setDriveDb (18.0f);
        engine.setBiteAmountPercent (65.0f);
        engine.setKneeSoftenPercent (40.0f);
        engine.setAsymmetryAmountPercent (40.0f);
        engine.setBiteTiltPercent (-25.0f);
        engine.setLevelDb (-3.0f);
        engine.setMixProportion (1.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::uint64 hash = 0;

        for (int block = 0; block < 16; ++block)
        {
            TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.25f, static_cast<juce::int64> (block) * 512);

            for (int sample = 0; sample < 512; ++sample)
                buffer.getWritePointer (1)[sample] *= 0.5f;

            juce::dsp::AudioBlock<float> block2 (buffer);
            engine.process (block2);

            if (block >= 8)
                hash ^= hashBuffer (buffer, 1469598103934665603ULL) + static_cast<juce::uint64> (block);
        }

        return hash;
    }

    // Energy (in dB) that is NOT within +/- toleranceBins of a harmonic of
    // fundamentalHz, measured over 20 Hz - 20 kHz with a Blackman-Harris
    // window (research-oversampling-architecture.md SS5 conventions).
    inline double nonHarmonicEnergyDb (const std::vector<float>& signal, double sampleRate, double fundamentalHz)
    {
        const int order = 12; // 4096-point
        const int fftSize = 1 << order;
        REQUIRE (static_cast<int> (signal.size()) >= fftSize);

        std::vector<float> scratch (static_cast<size_t> (2 * fftSize), 0.0f);
        std::copy (signal.end() - fftSize, signal.end(), scratch.begin());

        juce::dsp::WindowingFunction<float> window (static_cast<size_t> (fftSize),
                                                    juce::dsp::WindowingFunction<float>::blackmanHarris);
        window.multiplyWithWindowingTable (scratch.data(), static_cast<size_t> (fftSize));

        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (scratch.data());

        const auto binHz = sampleRate / static_cast<double> (fftSize);
        const auto lowBin = static_cast<int> (std::ceil (20.0 / binHz));
        const auto highBin = std::min (fftSize / 2, static_cast<int> (std::floor (20000.0 / binHz)));

        double energy = 0.0;

        for (int bin = lowBin; bin <= highBin; ++bin)
        {
            const auto frequency = static_cast<double> (bin) * binHz;
            const auto harmonic = std::round (frequency / fundamentalHz);
            const auto distanceBins = std::abs (frequency - harmonic * fundamentalHz) / binHz;

            if (harmonic >= 1.0 && distanceBins <= 4.0)
                continue; // skip the harmonic itself and its window skirt

            const auto magnitude = static_cast<double> (scratch[static_cast<size_t> (bin)]);
            energy += magnitude * magnitude;
        }

        return 10.0 * std::log10 (energy + 1.0e-30);
    }

    // Runs a Bite Tilt automation ramp at the given internal
    // parameter-update cadence and returns the resulting signal.
    // Automates Bite Tilt across its CUT half (-100% to 0%) while a steady
    // 1 kHz tone plays, so T-E1 below can compare the zipper artefacts of a
    // fine parameter-update cadence against a block-rate one.
    //
    // The cut half specifically, and not the full -100%..+100% travel this
    // used to sweep. Since v0.3.1 Bite Tilt's two directions have different
    // shelf-gain ceilings (biteTiltMaxCutDb 100 dB, biteTiltMaxBoostDb
    // 12 dB - see OvertureEngine.h for why), so a full-travel knob sweep is
    // no longer a symmetric 200 dB shelf-gain excursion and the zipper energy
    // it produces is not comparable to what T-E1's bound was calibrated
    // against. Sweeping the cut half keeps the excursion in the direction
    // whose scaling did not change, which is what makes the measurement -
    // and therefore T-E1's threshold - mean the same thing it always did.
    inline std::vector<float> runBiteTiltRamp (int subBlockSize)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;
        constexpr int numBlocks = 16;

        OvertureEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 1 };
        engine.prepare (spec);
        engine.setParameterUpdateSubBlockSize (subBlockSize);

        engine.setTightFrequencyHz (20.0f);
        engine.setDriveDb (0.0f);
        engine.setBiteAmountPercent (0.0f);
        engine.setKneeSoftenPercent (0.0f);
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setClipperVoicing (ClipperVoicing::softSymmetric);
        engine.setBiteTiltPercent (-100.0f);

        std::vector<float> output;
        output.reserve (static_cast<size_t> (numBlocks * blockSize));

        juce::AudioBuffer<float> buffer (1, blockSize);

        for (int block = 0; block < numBlocks; ++block)
        {
            const auto position = static_cast<float> (block) / static_cast<float> (numBlocks - 1);
            engine.setBiteTiltPercent (-100.0f + 100.0f * position);

            TestHelpers::fillWithSine (buffer, sampleRate, 1000.0, 0.25f, static_cast<juce::int64> (block) * blockSize);

            juce::dsp::AudioBlock<float> audioBlock (buffer);
            engine.process (audioBlock);

            const auto* data = buffer.getReadPointer (0);
            output.insert (output.end(), data, data + blockSize);
        }

        return output;
    }
}

TEST_CASE ("v0.3.0 is BIT-IDENTICAL to v0.2.0 for the three legacy voicings at every oversampling "
           "factor when the new parameters are left at their neutral defaults",
           "[dsp][engine][v030][golden]")
{
    // Goldens recorded from the v0.2.0 engine at origin/main (32c113d) with
    // the fixture in v030::runGoldenFixture(). These are what makes the
    // "existing sessions and presets are bit-identical" claim testable
    // rather than aspirational: they were produced by code that predates
    // every v0.3.0 change - the noise gate, the ADAA wrapper, the DC
    // blocker, the Feedback voicing dispatch and, above all, the move from
    // one coefficient update per host block to one per 32-sample sub-block.
    const juce::uint64 goldens[3][3] = {
        // 2x                    4x                      8x
        { 0xb6a28e59fc837e8cULL, 0x6fc017f0fefc3befULL, 0xc5788909ee3e4a7aULL }, // Asymmetric
        { 0x4bed3e50cfa357f7ULL, 0xb7b6c78fd95bc9c1ULL, 0x511f2c70b6d2f70bULL }, // Soft Symmetric
        { 0xd3b616e51aa8d8f9ULL, 0x45d915f8d709888aULL, 0x3455aa6d6caf7bb9ULL }, // Hard Clip
    };

    // The goldens are exact 64-bit hashes of raw float samples produced by
    // transcendental-heavy DSP: tanh()/exp() in the shapers and the polyphase
    // oversampling FIRs. Those last-ULP results are toolchain-specific -
    // Apple's libm and the MSVC UCRT are not obliged to agree on tanh(), and
    // the two compilers differ in how aggressively they contract a * b + c
    // into an FMA. Re-recording a second set of numbers on Windows would not
    // preserve what this test means: the v0.2.0 engine the goldens were
    // captured from (origin/main, 32c113d) only ever ran here, so a
    // Windows-recorded constant could only be captured from v0.3.0 code and
    // would assert nothing about v0.2.0 at all.
    //
    // So the byte-exact regression gate is scoped to its platform of record -
    // arm64 macOS, which is what CI's macos-latest job runs - and elsewhere
    // the fixture is still exercised for reproducibility. The claim that
    // v0.2.0 sessions keep behaving is additionally carried on every platform
    // by the exact T-S2 state-migration equivalence test and by the
    // neutral-default inertness tests.
#if JUCE_MAC && (defined (__aarch64__) || defined (__arm64__))
    constexpr bool goldensWereRecordedOnThisPlatform = true;
#else
    constexpr bool goldensWereRecordedOnThisPlatform = false;
#endif

    for (int voicing = 0; voicing <= 2; ++voicing)
    {
        for (int pow2 = 1; pow2 <= 3; ++pow2)
        {
            INFO ("voicing " << voicing << ", oversampling 2^" << pow2);

            const auto rendered = v030::runGoldenFixture (voicing, pow2);

            if (goldensWereRecordedOnThisPlatform)
                CHECK (rendered == goldens[voicing][pow2 - 1]);
            else
                CHECK (rendered == v030::runGoldenFixture (voicing, pow2));
        }
    }

    if (! goldensWereRecordedOnThisPlatform)
        WARN ("v0.2.0 byte-exact goldens are only asserted on arm64 macOS; this run checked "
              "fixture reproducibility only.");
}

TEST_CASE ("v0.3.0 sub-block parameter updates are transparent for static parameters: output is "
           "independent of the host block size",
           "[dsp][engine][v030]")
{
    // The 32-sample sub-block split only makes sense if it is inaudible when
    // nothing is moving. Every stage it touches (IIR filters, the polyphase
    // oversampler, the DryWetMixer delay line, juce::dsp::Gain) is
    // block-size invariant, so a settled engine must produce the same
    // samples whether the host hands it 512 or 37 at a time.
    const auto run = [] (int hostBlockSize, int subBlockSize)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int totalSamples = 4096;

        OvertureEngine engine;

        // Every value is set BEFORE prepare(), so prepare() pins each
        // smoother's current value to its target (see OvertureEngine::
        // prepare()) and nothing is ramping while the comparison runs. A
        // still-ramping smoother is legitimately sensitive to the block
        // grid - skip(n) lands on different points of the ramp - and that
        // sensitivity predates v0.3.0; what this test pins down is that the
        // SETTLED engine is grid-independent.
        engine.setTightFrequencyHz (120.0f);
        engine.setDriveDb (14.0f);
        engine.setBiteAmountPercent (65.0f);
        engine.setKneeSoftenPercent (40.0f);
        engine.setAsymmetryAmountPercent (40.0f);
        engine.setBiteTiltPercent (-30.0f);
        engine.setLevelDb (-1.0f);
        engine.setMixProportion (1.0f);

        juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };
        engine.prepare (spec);
        engine.setParameterUpdateSubBlockSize (subBlockSize);

        std::vector<float> output;
        output.reserve (static_cast<size_t> (totalSamples));

        juce::AudioBuffer<float> buffer (1, hostBlockSize);

        for (int offset = 0; offset < totalSamples; offset += hostBlockSize)
        {
            const auto length = std::min (hostBlockSize, totalSamples - offset);
            buffer.setSize (1, length, false, true, true);
            TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.3f, offset);

            juce::dsp::AudioBlock<float> audioBlock (buffer);
            engine.process (audioBlock);

            const auto* data = buffer.getReadPointer (0);
            output.insert (output.end(), data, data + length);
        }

        return output;
    };

    const auto diff = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        REQUIRE (a.size() == b.size());
        double worst = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (a[i]) - static_cast<double> (b[i])));
        return worst;
    };

    const auto reference = run (512, 32);

    for (const int hostBlockSize : { 64, 128, 37, 511 })
    {
        INFO ("host block size " << hostBlockSize);
        CHECK (diff (reference, run (hostBlockSize, 32)) == 0.0);
    }

    // ...and the same holds at the v0.2.0 cadence, which is what proves the
    // property belongs to the DSP graph rather than to the sub-block split.
    const auto legacyCadence = 1 << 20; // >= any block: one update per block, as v0.2.0 did
    CHECK (diff (run (512, legacyCadence), run (37, legacyCadence)) == 0.0);
}

// T-E1
TEST_CASE ("T-E1: sub-block parameter updates measurably reduce automation zipper on Bite Tilt",
           "[dsp][engine][v030]")
{
    const auto subBlockSignal = v030::runBiteTiltRamp (32);
    const auto blockRateSignal = v030::runBiteTiltRamp (512);

    const auto subBlockSprayDb = v030::nonHarmonicEnergyDb (subBlockSignal, 48000.0, 1000.0);
    const auto blockRateSprayDb = v030::nonHarmonicEnergyDb (blockRateSignal, 48000.0, 1000.0);

    INFO ("non-harmonic spray: 32-sample sub-blocks " << subBlockSprayDb
          << " dB, 512-sample block rate " << blockRateSprayDb << " dB");

    // The two runs must actually differ (otherwise the test is measuring
    // nothing), and the finer cadence must be the quieter one by a clear
    // margin.
    CHECK (blockRateSprayDb > subBlockSprayDb);
    CHECK (blockRateSprayDb - subBlockSprayDb >= 12.0);
}

// T-E2
TEST_CASE ("T-E2: Bite and Knee Soften are exactly inert in the Feedback voicing",
           "[dsp][engine][v030][feedback]")
{
    const auto run = [] (float biteAmountPercent, float kneeSoftenPercent)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        OvertureEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, blockSize, 1 };
        engine.prepare (spec);

        engine.setClipperVoicing (ClipperVoicing::feedback);
        engine.setTightFrequencyHz (100.0f);
        engine.setDriveDb (12.0f);
        engine.setAsymmetryAmountPercent (40.0f);
        engine.setBiteTiltPercent (0.0f);
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);
        engine.setBiteAmountPercent (biteAmountPercent);
        engine.setKneeSoftenPercent (kneeSoftenPercent);

        std::vector<float> output;
        juce::AudioBuffer<float> buffer (1, blockSize);

        for (int block = 0; block < 8; ++block)
        {
            TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.25f, static_cast<juce::int64> (block) * blockSize);

            juce::dsp::AudioBlock<float> audioBlock (buffer);
            engine.process (audioBlock);

            const auto* data = buffer.getReadPointer (0);
            output.insert (output.end(), data, data + blockSize);
        }

        return output;
    };

    const auto reference = run (0.0f, 0.0f);

    for (const auto pair : { std::pair<float, float> { 100.0f, 0.0f },
                             std::pair<float, float> { 0.0f, 100.0f },
                             std::pair<float, float> { 100.0f, 100.0f },
                             std::pair<float, float> { 43.0f, 77.0f } })
    {
        INFO ("bite " << pair.first << "%, knee " << pair.second << "%");

        const auto candidate = run (pair.first, pair.second);
        REQUIRE (candidate.size() == reference.size());

        double worst = 0.0;
        for (size_t i = 0; i < reference.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (reference[i]) - static_cast<double> (candidate[i])));

        // Bit-identical: the circuit's own 720 Hz pre-emphasis IS the bite
        // mechanism, and its knee is physical - neither control has anything
        // to act on.
        CHECK (worst == 0.0);
    }
}

// T-E3
TEST_CASE ("T-E3: gate + Feedback voicing + Enhanced quality survive a 30 s bursts/silence/square soak "
           "with no NaN, Inf or denormal stall",
           "[dsp][engine][v030][robustness]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = static_cast<int> (30.0 * sampleRate / blockSize);

    OvertureEngine engine;
    juce::dsp::ProcessSpec spec { sampleRate, blockSize, 2 };
    engine.prepare (spec);

    engine.setClipperVoicing (ClipperVoicing::feedback);
    engine.setClipQualityMode (ClipQualityMode::enhanced);
    engine.setKneeResponseMode (KneeResponseMode::signal);
    engine.setGateEnabled (true);
    engine.setGateThresholdDb (-45.0f);
    engine.setGateReleaseMode (basilica::dsp::NoiseGate::ReleaseMode::automatic);
    engine.setDriveDb (30.0f);
    engine.setTightFrequencyHz (150.0f);
    engine.setBiteTiltPercent (-40.0f);
    engine.setLevelDb (0.0f);
    engine.setMixProportion (1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::Random random (0x0ec12345);

    std::vector<double> blockMicroseconds;
    blockMicroseconds.reserve (static_cast<size_t> (numBlocks));

    bool allFinite = true;

    for (int block = 0; block < numBlocks; ++block)
    {
        const auto phase = block % 60;

        for (int channel = 0; channel < 2; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto index = static_cast<juce::int64> (block) * blockSize + sample;

                if (phase < 20)
                {
                    // Silence (the denormal trap: the gate closes and the
                    // whole chain runs on vanishing values).
                    data[sample] = 0.0f;
                }
                else if (phase < 40)
                {
                    // Full-scale square - the worst case for the Newton
                    // solver (an instantaneous multi-volt step across the
                    // diode network every half period).
                    data[sample] = ((index / 109) % 2 == 0) ? 1.0f : -1.0f;
                }
                else
                {
                    // Decaying noise burst.
                    const auto envelope = std::exp (-static_cast<double> (phase - 40) * 0.25);
                    data[sample] = static_cast<float> (envelope * (random.nextDouble() * 2.0 - 1.0));
                }
            }
        }

        const auto start = juce::Time::getHighResolutionTicks();

        juce::dsp::AudioBlock<float> audioBlock (buffer);
        engine.process (audioBlock);

        blockMicroseconds.push_back (juce::Time::highResolutionTicksToSeconds (
                                         juce::Time::getHighResolutionTicks() - start) * 1.0e6);

        if (! TestHelpers::allSamplesFinite (buffer))
            allFinite = false;
    }

    CHECK (allFinite);

    // Denormal-stall guard: compare the median cost of the silent blocks
    // against the median cost of the loud ones. A denormal stall shows up as
    // silent blocks becoming dramatically SLOWER than loud ones; anything
    // within an order of magnitude is normal cache/branch variation.
    std::vector<double> silentBlocks;
    std::vector<double> loudBlocks;

    for (size_t i = 0; i < blockMicroseconds.size(); ++i)
        ((static_cast<int> (i) % 60) < 20 ? silentBlocks : loudBlocks).push_back (blockMicroseconds[i]);

    const auto median = [] (std::vector<double> values)
    {
        REQUIRE (! values.empty());
        std::sort (values.begin(), values.end());
        return values[values.size() / 2];
    };

    const auto silentMedian = median (silentBlocks);
    const auto loudMedian = median (loudBlocks);

    INFO ("median block time: silence " << silentMedian << " us, loud " << loudMedian << " us");
    CHECK (silentMedian < loudMedian * 3.0);
}
