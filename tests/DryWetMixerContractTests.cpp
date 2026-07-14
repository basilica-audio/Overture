#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_test_macros.hpp>

// These tests document a JUCE 8.0.14 juce::dsp::DryWetMixer behaviour that
// TightBoostEngine::prepare() depends on and works around: the mixer's
// internal dry/wet gain smoothers default their *target* to fully wet
// (mix == 1.0) until setWetMixProportion() is called at least once, and
// DryWetMixer::reset() (invoked by its own prepare()) only snaps the
// smoothers' *current* value to whatever *target* happens to be set at that
// moment - it does not know anything about a caller's real, non-default mix
// value. Skipping the priming setWetMixProportion() call before prepare()
// finishes would leave a fresh mixer audibly ramping in from 100% wet over
// its internal 50ms default ramp, regardless of what the actual Mix
// parameter was set to. See TightBoostEngine::prepare() for the fix.
TEST_CASE ("DryWetMixer contract: without priming, a fresh mixer starts fully wet", "[dsp][drywetmixer][contract]")
{
    juce::dsp::DryWetMixer<float> mixer (64);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 1;

    mixer.prepare (spec);
    mixer.setWetLatency (0.0f);
    // Deliberately NOT calling setWetMixProportion() here, to document the
    // "before priming" starting state.

    juce::AudioBuffer<float> dryBuffer (1, 128);
    dryBuffer.clear(); // dry = 0

    juce::dsp::AudioBlock<float> dryBlock (dryBuffer);
    mixer.pushDrySamples (dryBlock);

    juce::AudioBuffer<float> wetBuffer (1, 128);
    for (int i = 0; i < 128; ++i)
        wetBuffer.setSample (0, i, 10.0f); // wet = 10, obviously distinct from dry

    juce::dsp::AudioBlock<float> wetBlock (wetBuffer);
    mixer.mixWetSamples (wetBlock);

    // Fully wet (dry is silent here, so output == wet == 10) at the very
    // first sample of a freshly prepared, unprimed mixer.
    CHECK (wetBuffer.getSample (0, 0) > 9.0f);
}

TEST_CASE ("DryWetMixer contract: priming setWetMixProportion() before prepare()'s reset() "
           "makes the mixer honour the target from sample 0",
           "[dsp][drywetmixer][contract]")
{
    juce::dsp::DryWetMixer<float> mixer (64);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 1;

    // This is the fix TightBoostEngine::prepare() applies: prepare(), then
    // set the real target, then reset() so the smoothers snap current ==
    // target immediately rather than ramping in from the 1.0 default.
    mixer.prepare (spec);
    mixer.setWetLatency (0.0f);
    mixer.setWetMixProportion (0.0f); // fully dry
    mixer.reset();

    juce::AudioBuffer<float> dryBuffer (1, 128);
    dryBuffer.clear(); // dry = 0

    juce::dsp::AudioBlock<float> dryBlock (dryBuffer);
    mixer.pushDrySamples (dryBlock);

    juce::AudioBuffer<float> wetBuffer (1, 128);
    for (int i = 0; i < 128; ++i)
        wetBuffer.setSample (0, i, 10.0f);

    juce::dsp::AudioBlock<float> wetBlock (wetBuffer);
    mixer.mixWetSamples (wetBlock);

    // Fully dry (dry is silent here) from the very first sample, no ramp-in.
    CHECK (wetBuffer.getSample (0, 0) < 1.0e-6f);
    CHECK (wetBuffer.getSample (0, 127) < 1.0e-6f);
}

TEST_CASE ("DryWetMixer contract: setWetMixProportion() alone (no reset()) still ramps over ~50ms",
           "[dsp][drywetmixer][contract]")
{
    // Complements the two tests above: calling setWetMixProportion() only
    // changes the *target* of the internal dry/wet gain smoothers - it does
    // not, by itself, snap their *current* value. Without a following
    // reset(), a fresh mixer's default-wet current value (see the first
    // test above) is still ramping towards the requested proportion tens of
    // milliseconds later. This is why TightBoostEngine::prepare() calls
    // setWetMixProportion() *and* relies on the subsequent reset() (not
    // just the setter) to make the target take effect immediately.
    juce::dsp::DryWetMixer<float> mixer (64);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 1;

    mixer.prepare (spec); // internal current/target both default to fully wet
    mixer.setWetLatency (0.0f);
    mixer.setWetMixProportion (0.0f); // requests fully dry, but no reset() follows

    juce::AudioBuffer<float> dryBuffer (1, 128);
    dryBuffer.clear();

    juce::dsp::AudioBlock<float> dryBlock (dryBuffer);
    mixer.pushDrySamples (dryBlock);

    juce::AudioBuffer<float> wetBuffer (1, 128);
    for (int i = 0; i < 128; ++i)
        wetBuffer.setSample (0, i, 10.0f);

    juce::dsp::AudioBlock<float> wetBlock (wetBuffer);
    mixer.mixWetSamples (wetBlock);

    // Still audibly close to fully wet after only 128 samples of a ~2400-
    // sample (0.05s @ 48kHz) ramp - proving the setter alone is not enough.
    CHECK (wetBuffer.getSample (0, 20) > 8.0f);
}
