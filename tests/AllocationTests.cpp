#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "dsp/OvertureEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// Permanent audio-thread allocation regression guard (basilica-audio/
// Overture issue #12): OvertureEngine::process() used to unconditionally
// call juce::dsp::IIR::Coefficients<float>::makeHighPass/makeLowPass for
// the Tight HPF and both Tone LPF stages every block - each call `new`s a
// fresh ref-counted Coefficients object (plus its own heap-backed Array),
// so up to 6 allocations/6 deallocations happened per processBlock() call.
// Neither pluginval nor auval do allocation-instrumented profiling, and
// none of the other pre-existing Catch2 tests had an allocation-counting
// mechanism, so this passed CI clean before. This test exercises the full
// plugin with automated Tight/Bite/Knee Soften/Asymmetry/Bite Tilt/Drive/Mix
// parameters (so the smoothers keep re-deriving coefficients every block,
// exactly the code path issue #12 was in, extended in v0.2.0 to the new
// Bite/Bite Tilt shelf filters, which use the same non-allocating
// ArrayCoefficients + ovtr::applyBiquadCoefficients pattern - see
// src/dsp/OvertureEngine.cpp/RealtimeCoefficients.h) and fails if
// processBlock() ever touches the heap again.
namespace
{
    void setParam (OvertureAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("OvertureAudioProcessor::processBlock allocates no memory while Tight/Bite/Knee Soften/"
           "Asymmetry/Bite Tilt are moving",
           "[dsp][rt-safety][alloc]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::drive, 20.0f);
    setParam (processor, ParamIDs::mix, 100.0f);
    // Touch every v0.2.0 parameter at least once here, before the guard
    // starts - setValueNotifyingHost()'s very first call for a given
    // parameter can lazily warm up internal JUCE bookkeeping (observed:
    // exactly one allocation the first time a brand-new parameter ID was
    // first notified from inside the guarded loop below), the same reason
    // Drive/Mix above are primed before the loop rather than left at their
    // untouched defaults.
    setParam (processor, ParamIDs::biteAmount, 10.0f);
    setParam (processor, ParamIDs::kneeSoften, 10.0f);
    setParam (processor, ParamIDs::asymmetryAmount, 10.0f);
    setParam (processor, ParamIDs::biteTilt, -10.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // Allocation during prepareToPlay()/parameter smoothing settle is
    // expected and allowed - only the steady-state per-block behaviour
    // below is guarded.
    for (int warmup = 0; warmup < 4; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 32; ++block)
    {
        // Continuously move every v0.2.0 coefficient-recomputing/blend
        // control every block - this is exactly the "smoothers keep
        // re-deriving coefficients every block" scenario issue #12 was
        // filed against (a fixed/settled parameter wouldn't exercise the
        // bug once its smoother reaches target), extended to the new Bite
        // shelf (inside the oversampled block) and Bite Tilt shelf
        // (post-clip) coefficient updates, and the always-nonzero-here Knee
        // Soften/Asymmetry blend path.
        const auto sweep = static_cast<float> (block) / 32.0f;
        setParam (processor, ParamIDs::tight, 20.0f + sweep * 380.0f);
        setParam (processor, ParamIDs::biteAmount, sweep * 100.0f);
        setParam (processor, ParamIDs::kneeSoften, sweep * 100.0f);
        setParam (processor, ParamIDs::asymmetryAmount, sweep * 100.0f);
        setParam (processor, ParamIDs::biteTilt, -100.0f + sweep * 200.0f);

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

TEST_CASE ("OvertureEngine::process allocates no memory across repeated blocks", "[dsp][engine][rt-safety][alloc]")
{
    // Isolated from PluginProcessor/APVTS so this attributes any regression
    // specifically to OvertureEngine's own coefficient recompute (basilica-
    // audio/Overture issue #12, extended in v0.2.0 to Bite/Bite Tilt),
    // independent of the processor's parameter plumbing.
    OvertureEngine engine;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    engine.prepare (spec);

    engine.setDriveDb (20.0f);
    engine.setMixProportion (1.0f);
    engine.setTightFrequencyHz (300.0f);
    engine.setBiteAmountPercent (50.0f);
    engine.setKneeSoftenPercent (50.0f);
    engine.setAsymmetryAmountPercent (50.0f);
    engine.setBiteTiltPercent (-20.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);

    // Warm-up block outside the guard, as above.
    engine.process (block);

    TestAlloc::AllocationGuard guard;

    for (int i = 0; i < 32; ++i)
    {
        // Retarget every v0.2.0 control every block so the smoothers stay
        // in motion and process() keeps re-deriving filter coefficients,
        // the same steady-state condition the processor-level test above
        // exercises.
        const auto sweep = static_cast<float> (i) / 32.0f;
        engine.setTightFrequencyHz (20.0f + sweep * 380.0f);
        engine.setBiteAmountPercent (sweep * 100.0f);
        engine.setKneeSoftenPercent (sweep * 100.0f);
        engine.setAsymmetryAmountPercent (sweep * 100.0f);
        engine.setBiteTiltPercent (-100.0f + sweep * 200.0f);

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, static_cast<juce::int64> (i) * 512);
        engine.process (block);
    }

    CHECK (guard.count() == 0);
}

//==============================================================================
// v0.3.0 T-P1: the same permanent guard, extended to every new code path.
//
// The v0.3.0 stages are exactly the kind of code that quietly allocates:
// the noise gate keeps per-channel filter state, the Feedback voicing runs a
// per-channel Newton solver, the ADAA wrapper keeps per-channel history, and
// the sub-block loop calls into the oversampler 16x more often per block
// than v0.2.0 did. All of them size themselves in prepare() and must never
// touch the heap afterwards.
TEST_CASE ("T-P1: processBlock allocates nothing with the gate on, the Feedback voicing selected and "
           "Enhanced clip quality, while every parameter is mid-automation",
           "[dsp][rt-safety][alloc][v030]")
{
    OvertureAudioProcessor processor;

    setParam (processor, ParamIDs::voicing, static_cast<float> (ClipperVoicing::feedback));
    setParam (processor, ParamIDs::clipQuality, static_cast<float> (ClipQualityMode::enhanced));
    setParam (processor, ParamIDs::kneeResponse, static_cast<float> (KneeResponseMode::signal));
    setParam (processor, ParamIDs::gate, 1.0f);
    setParam (processor, ParamIDs::gateThreshold, -45.0f);
    setParam (processor, ParamIDs::gateRelease, 0.0f);
    setParam (processor, ParamIDs::drive, 20.0f);
    setParam (processor, ParamIDs::mix, 100.0f);
    setParam (processor, ParamIDs::biteAmount, 10.0f);
    setParam (processor, ParamIDs::kneeSoften, 10.0f);
    setParam (processor, ParamIDs::asymmetryAmount, 10.0f);
    setParam (processor, ParamIDs::biteTilt, -10.0f);
    setParam (processor, ParamIDs::tight, 100.0f);

    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int warmup = 0; warmup < 8; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 32; ++block)
    {
        const auto sweep = static_cast<float> (block) / 32.0f;
        setParam (processor, ParamIDs::tight, 20.0f + sweep * 380.0f);
        setParam (processor, ParamIDs::drive, sweep * 40.0f);
        setParam (processor, ParamIDs::biteAmount, sweep * 100.0f);
        setParam (processor, ParamIDs::kneeSoften, sweep * 100.0f);
        setParam (processor, ParamIDs::asymmetryAmount, sweep * 100.0f);
        setParam (processor, ParamIDs::biteTilt, -100.0f + sweep * 200.0f);
        setParam (processor, ParamIDs::gateThreshold, -80.0f + sweep * 60.0f);

        // Alternate loud programme and silence so the gate's state machine
        // opens and closes inside the guarded region.
        const auto amplitude = (block / 4) % 2 == 0 ? 0.5f : 0.0f;
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, amplitude, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

TEST_CASE ("T-P1: the oversized-block chunk guard still holds with the gate engaged and the Feedback "
           "voicing selected",
           "[dsp][rt-safety][alloc][v030]")
{
    // Issue #13's defensive chunking has to survive the v0.3.0 rework: a
    // host that hands over more samples than it promised must produce
    // exactly what the same input produces when correctly chunked - and must
    // still not allocate.
    const auto run = [] (int hostBlockSize)
    {
        constexpr int totalSamples = 2048;

        OvertureEngine engine;
        engine.setTightFrequencyHz (100.0f);
        engine.setDriveDb (18.0f);
        engine.setBiteAmountPercent (0.0f);
        engine.setKneeSoftenPercent (0.0f);
        engine.setAsymmetryAmountPercent (40.0f);
        engine.setBiteTiltPercent (0.0f);
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);

        juce::dsp::ProcessSpec spec { 48000.0, 512, 1 };
        engine.prepare (spec);

        engine.setClipperVoicing (ClipperVoicing::feedback);
        engine.setClipQualityMode (ClipQualityMode::enhanced);
        engine.setGateEnabled (true);
        engine.setGateThresholdDb (-60.0f);

        std::vector<float> output;
        juce::AudioBuffer<float> buffer (1, hostBlockSize);

        for (int offset = 0; offset < totalSamples; offset += hostBlockSize)
        {
            const auto length = juce::jmin (hostBlockSize, totalSamples - offset);
            buffer.setSize (1, length, false, true, true);
            TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.4f, offset);

            juce::dsp::AudioBlock<float> audioBlock (buffer);
            engine.process (audioBlock);

            const auto* data = buffer.getReadPointer (0);
            output.insert (output.end(), data, data + length);
        }

        return output;
    };

    // 2048 is 4x the promised maximum block size - the engine has to split
    // it internally and land on exactly the host-chunked result.
    const auto oversized = run (2048);
    const auto chunked = run (512);

    REQUIRE (oversized.size() == chunked.size());

    double worst = 0.0;
    for (size_t i = 0; i < oversized.size(); ++i)
        worst = std::max (worst, std::abs (static_cast<double> (oversized[i]) - static_cast<double> (chunked[i])));

    CHECK (worst == 0.0);
}
