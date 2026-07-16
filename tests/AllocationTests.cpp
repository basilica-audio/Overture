#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "dsp/OvertureEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

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
