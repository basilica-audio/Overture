#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

TEST_CASE ("State round-trip preserves non-default values of every parameter", "[state]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* tightParam = processor.apvts.getParameter (ParamIDs::tight);
    auto* driveParam = processor.apvts.getParameter (ParamIDs::drive);
    auto* biteAmountParam = processor.apvts.getParameter (ParamIDs::biteAmount);
    auto* kneeSoftenParam = processor.apvts.getParameter (ParamIDs::kneeSoften);
    auto* asymmetryAmountParam = processor.apvts.getParameter (ParamIDs::asymmetryAmount);
    auto* biteTiltParam = processor.apvts.getParameter (ParamIDs::biteTilt);
    auto* levelParam = processor.apvts.getParameter (ParamIDs::level);
    auto* mixParam = processor.apvts.getParameter (ParamIDs::mix);
    auto* bypassParam = processor.apvts.getParameter (ParamIDs::bypass);
    auto* voicingParam = processor.apvts.getParameter (ParamIDs::voicing);
    auto* oversamplingParam = processor.apvts.getParameter (ParamIDs::oversampling);

    REQUIRE (tightParam != nullptr);
    REQUIRE (driveParam != nullptr);
    REQUIRE (biteAmountParam != nullptr);
    REQUIRE (kneeSoftenParam != nullptr);
    REQUIRE (asymmetryAmountParam != nullptr);
    REQUIRE (biteTiltParam != nullptr);
    REQUIRE (levelParam != nullptr);
    REQUIRE (mixParam != nullptr);
    REQUIRE (bypassParam != nullptr);
    REQUIRE (voicingParam != nullptr);
    REQUIRE (oversamplingParam != nullptr);

    tightParam->setValueNotifyingHost (tightParam->convertTo0to1 (250.0f));
    driveParam->setValueNotifyingHost (driveParam->convertTo0to1 (33.0f));
    biteAmountParam->setValueNotifyingHost (biteAmountParam->convertTo0to1 (82.0f));
    kneeSoftenParam->setValueNotifyingHost (kneeSoftenParam->convertTo0to1 (17.0f));
    asymmetryAmountParam->setValueNotifyingHost (asymmetryAmountParam->convertTo0to1 (73.0f));
    biteTiltParam->setValueNotifyingHost (biteTiltParam->convertTo0to1 (-42.0f));
    levelParam->setValueNotifyingHost (levelParam->convertTo0to1 (-6.5f));
    mixParam->setValueNotifyingHost (mixParam->convertTo0to1 (42.0f));
    bypassParam->setValueNotifyingHost (1.0f); // non-default: on
    voicingParam->setValueNotifyingHost (voicingParam->convertTo0to1 (2.0f)); // "Hard Clip"
    oversamplingParam->setValueNotifyingHost (oversamplingParam->convertTo0to1 (2.0f)); // "8x"

    const auto savedTight = tightParam->getValue();
    const auto savedDrive = driveParam->getValue();
    const auto savedBiteAmount = biteAmountParam->getValue();
    const auto savedKneeSoften = kneeSoftenParam->getValue();
    const auto savedAsymmetryAmount = asymmetryAmountParam->getValue();
    const auto savedBiteTilt = biteTiltParam->getValue();
    const auto savedLevel = levelParam->getValue();
    const auto savedMix = mixParam->getValue();
    const auto savedBypass = bypassParam->getValue();
    const auto savedVoicing = voicingParam->getValue();
    const auto savedOversampling = oversamplingParam->getValue();

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    tightParam->setValueNotifyingHost (tightParam->getDefaultValue());
    driveParam->setValueNotifyingHost (driveParam->getDefaultValue());
    biteAmountParam->setValueNotifyingHost (biteAmountParam->getDefaultValue());
    kneeSoftenParam->setValueNotifyingHost (kneeSoftenParam->getDefaultValue());
    asymmetryAmountParam->setValueNotifyingHost (asymmetryAmountParam->getDefaultValue());
    biteTiltParam->setValueNotifyingHost (biteTiltParam->getDefaultValue());
    levelParam->setValueNotifyingHost (levelParam->getDefaultValue());
    mixParam->setValueNotifyingHost (mixParam->getDefaultValue());
    bypassParam->setValueNotifyingHost (bypassParam->getDefaultValue());
    voicingParam->setValueNotifyingHost (voicingParam->getDefaultValue());
    oversamplingParam->setValueNotifyingHost (oversamplingParam->getDefaultValue());

    REQUIRE (tightParam->getValue() != Catch::Approx (savedTight));
    REQUIRE (driveParam->getValue() != Catch::Approx (savedDrive));
    REQUIRE (biteAmountParam->getValue() != Catch::Approx (savedBiteAmount));
    REQUIRE (kneeSoftenParam->getValue() != Catch::Approx (savedKneeSoften));
    REQUIRE (asymmetryAmountParam->getValue() != Catch::Approx (savedAsymmetryAmount));
    REQUIRE (biteTiltParam->getValue() != Catch::Approx (savedBiteTilt));
    REQUIRE (levelParam->getValue() != Catch::Approx (savedLevel));
    REQUIRE (mixParam->getValue() != Catch::Approx (savedMix));
    REQUIRE (bypassParam->getValue() != Catch::Approx (savedBypass));
    REQUIRE (voicingParam->getValue() != Catch::Approx (savedVoicing));
    REQUIRE (oversamplingParam->getValue() != Catch::Approx (savedOversampling));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (tightParam->getValue() == Catch::Approx (savedTight).margin (1e-6));
    CHECK (driveParam->getValue() == Catch::Approx (savedDrive).margin (1e-6));
    CHECK (biteAmountParam->getValue() == Catch::Approx (savedBiteAmount).margin (1e-6));
    CHECK (kneeSoftenParam->getValue() == Catch::Approx (savedKneeSoften).margin (1e-6));
    CHECK (asymmetryAmountParam->getValue() == Catch::Approx (savedAsymmetryAmount).margin (1e-6));
    CHECK (biteTiltParam->getValue() == Catch::Approx (savedBiteTilt).margin (1e-6));
    CHECK (levelParam->getValue() == Catch::Approx (savedLevel).margin (1e-6));
    CHECK (mixParam->getValue() == Catch::Approx (savedMix).margin (1e-6));
    CHECK (bypassParam->getValue() == Catch::Approx (savedBypass).margin (1e-6));
    CHECK (voicingParam->getValue() == Catch::Approx (savedVoicing).margin (1e-6));
    CHECK (oversamplingParam->getValue() == Catch::Approx (savedOversampling).margin (1e-6));
}

//==============================================================================
// v0.2.0 state migration guarantee (docs/design-brief.md guarantee 6):
// a v0.1-only saved session (only `tone`, no `biteTilt`/`biteAmount`/
// `kneeSoften`/`asymmetryAmount`) must load without crashing, `tone`'s value
// must be lossily mapped into an equivalent `biteTilt` value per the
// documented migration rule (OvertureAudioProcessor::setStateInformation()),
// and every other new parameter must fall back to its v0.2.0 default, not
// zero/garbage.
namespace
{
    // Builds a minimal, well-formed v0.1-shaped APVTS XML tree by hand
    // (rather than via a real v0.1 binary, which no longer exists in this
    // repo) - a <PARAMETERS> root with only the v0.1 parameter IDs,
    // including `tone` at a specific test value and deliberately omitting
    // every v0.2.0-only ID, exactly mirroring what a genuine old session
    // saved by v0.1.0 would contain (see docs/architecture.md's ADR on the
    // APVTS XML format - "value" is the plain/denormalised parameter value,
    // JUCE 8.0.14 AudioProcessorValueTreeState.cpp's
    // valuePropertyID/setDenormalisedValue).
    juce::MemoryBlock buildLegacyV01State (float legacyToneHz)
    {
        juce::XmlElement root ("PARAMETERS");

        const auto addParam = [&] (const char* id, double value)
        {
            auto* param = new juce::XmlElement ("PARAM");
            param->setAttribute ("id", id);
            param->setAttribute ("value", value);
            root.addChildElement (param);
        };

        addParam (ParamIDs::tight, 130.0);
        addParam (ParamIDs::drive, 8.0);
        addParam (ParamIDs::tone, static_cast<double> (legacyToneHz));
        addParam (ParamIDs::level, 0.0);
        addParam (ParamIDs::mix, 100.0);
        addParam (ParamIDs::bypass, 0.0);
        addParam (ParamIDs::voicing, 0.0);
        addParam (ParamIDs::oversampling, 1.0);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (root, block);
        return block;
    }
}

TEST_CASE ("State migration: a v0.1-only session (tone, no biteTilt) loads without crashing", "[state][migration]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto legacyState = buildLegacyV01State (4500.0f);
    CHECK_NOTHROW (processor.setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize())));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
}

TEST_CASE ("State migration: legacy tone=1000 Hz (v0.1 fully-closed) maps to biteTilt=-100% (maximally dark)",
           "[state][migration]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto legacyState = buildLegacyV01State (1000.0f);
    processor.setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize()));

    auto* biteTiltParam = processor.apvts.getParameter (ParamIDs::biteTilt);
    REQUIRE (biteTiltParam != nullptr);
    CHECK (biteTiltParam->convertFrom0to1 (biteTiltParam->getValue()) == Catch::Approx (-100.0f).margin (0.5f));
}

TEST_CASE ("State migration: legacy tone=8000 Hz (v0.1 fully-open) maps to biteTilt=0% (flat)", "[state][migration]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto legacyState = buildLegacyV01State (8000.0f);
    processor.setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize()));

    auto* biteTiltParam = processor.apvts.getParameter (ParamIDs::biteTilt);
    REQUIRE (biteTiltParam != nullptr);
    CHECK (biteTiltParam->convertFrom0to1 (biteTiltParam->getValue()) == Catch::Approx (0.0f).margin (0.5f));
}

TEST_CASE ("State migration: legacy tone=4500 Hz (midpoint) maps to an intermediate negative biteTilt",
           "[state][migration]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto legacyState = buildLegacyV01State (4500.0f);
    processor.setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize()));

    auto* biteTiltParam = processor.apvts.getParameter (ParamIDs::biteTilt);
    REQUIRE (biteTiltParam != nullptr);

    const auto migrated = biteTiltParam->convertFrom0to1 (biteTiltParam->getValue());
    CHECK (migrated < 0.0f);  // v0.1's Tone could only ever darken - migration must never produce a positive/bright value
    CHECK (migrated > -100.0f);
}

TEST_CASE ("State migration: new-in-v0.2.0 parameters fall back to their own defaults, not zero/garbage",
           "[state][migration]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Perturb every new parameter away from its default first, so this test
    // can't pass by accident (parameters already sitting at their default
    // before the legacy load).
    for (const auto* id : { ParamIDs::biteAmount, ParamIDs::kneeSoften, ParamIDs::asymmetryAmount })
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (5.0f));
    }

    const auto legacyState = buildLegacyV01State (8000.0f);
    processor.setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize()));

    auto* biteAmountParam = processor.apvts.getParameter (ParamIDs::biteAmount);
    auto* kneeSoftenParam = processor.apvts.getParameter (ParamIDs::kneeSoften);
    auto* asymmetryAmountParam = processor.apvts.getParameter (ParamIDs::asymmetryAmount);

    REQUIRE (biteAmountParam != nullptr);
    REQUIRE (kneeSoftenParam != nullptr);
    REQUIRE (asymmetryAmountParam != nullptr);

    CHECK (biteAmountParam->convertFrom0to1 (biteAmountParam->getValue()) == Catch::Approx (65.0f).margin (1e-3));
    CHECK (kneeSoftenParam->convertFrom0to1 (kneeSoftenParam->getValue()) == Catch::Approx (40.0f).margin (1e-3));
    CHECK (asymmetryAmountParam->convertFrom0to1 (asymmetryAmountParam->getValue()) == Catch::Approx (40.0f).margin (1e-3));
}

TEST_CASE ("State migration: v0.2.0 state with unknown-to-v0.1 IDs still round-trips its own known IDs "
           "(forward-tolerant, same unknown-ID-ignored pattern as the rest of the suite)",
           "[state][migration]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* biteTiltParam = processor.apvts.getParameter (ParamIDs::biteTilt);
    REQUIRE (biteTiltParam != nullptr);
    biteTiltParam->setValueNotifyingHost (biteTiltParam->convertTo0to1 (33.0f));

    juce::MemoryBlock v02State;
    processor.getStateInformation (v02State);

    // A hypothetical older build would simply not recognise `biteTilt`/
    // `biteAmount`/etc. - simulated here not by constructing a fake old
    // processor (out of scope), but by confirming v0.2.0's own round-trip
    // through its own state is unaffected by the presence of IDs a v0.1
    // build wouldn't have known (the state blob already contains exactly
    // those IDs, since v0.1's `tone` param no longer exists to write).
    OvertureAudioProcessor freshProcessor;
    freshProcessor.prepareToPlay (48000.0, 512);
    CHECK_NOTHROW (freshProcessor.setStateInformation (v02State.getData(), static_cast<int> (v02State.getSize())));

    auto* freshBiteTiltParam = freshProcessor.apvts.getParameter (ParamIDs::biteTilt);
    REQUIRE (freshBiteTiltParam != nullptr);
    CHECK (freshBiteTiltParam->convertFrom0to1 (freshBiteTiltParam->getValue()) == Catch::Approx (33.0f).margin (1e-3));
}

//==============================================================================
// v0.3.0 state-schema tests (brief SS6, T-S1/T-S2/T-S4/T-S5).
//
// The contract these pin down: v0.3.0 adds five parameters and stamps a
// stateSchema="3" attribute on saved state, but performs NO value rewriting
// on load, because every new parameter's default IS the v0.2.0-equivalent
// neutral value. That only holds if (a) the defaults really are neutral and
// (b) an engine configured from a v0.2.0-shaped state produces bit-identical
// audio - so both are asserted directly rather than reasoned about.

namespace
{
    // Synthesises the XML a v0.2.0 build would have written: the APVTS root
    // tag, PARAM nodes for the eleven v0.2.0 parameters only, and no
    // stateSchema attribute.
    juce::MemoryBlock makeV020StateBlob (const juce::AudioProcessorValueTreeState& apvts,
                                         float tightHz,
                                         float driveDb,
                                         float biteAmount,
                                         float kneeSoften,
                                         float asymmetryAmount,
                                         float biteTilt,
                                         float levelDb,
                                         float mixPercent,
                                         int voicingIndex,
                                         int oversamplingIndex)
    {
        juce::XmlElement xml (apvts.state.getType().toString());

        const auto addParam = [&xml] (const char* id, double value)
        {
            auto* node = xml.createNewChildElement ("PARAM");
            node->setAttribute ("id", id);
            node->setAttribute ("value", value);
        };

        addParam (ParamIDs::tight, tightHz);
        addParam (ParamIDs::drive, driveDb);
        addParam (ParamIDs::biteAmount, biteAmount);
        addParam (ParamIDs::kneeSoften, kneeSoften);
        addParam (ParamIDs::asymmetryAmount, asymmetryAmount);
        addParam (ParamIDs::biteTilt, biteTilt);
        addParam (ParamIDs::level, levelDb);
        addParam (ParamIDs::mix, mixPercent);
        addParam (ParamIDs::bypass, 0.0);
        addParam (ParamIDs::voicing, voicingIndex);
        addParam (ParamIDs::oversampling, oversamplingIndex);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (xml, block);
        return block;
    }

    void fillTestProgram (juce::AudioBuffer<float>& buffer, double sampleRate, juce::int64 startSample)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto t = static_cast<double> (startSample + sample) / sampleRate;
                // -12 dBFS composite programme (fundamental + a fifth + a
                // low octave), scaled per channel so the two channels are
                // not identical.
                const auto value = 0.25 * (0.6 * std::sin (juce::MathConstants<double>::twoPi * 82.4 * t)
                                           + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)
                                           + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 1244.5 * t));
                data[sample] = static_cast<float> (value * (channel == 0 ? 1.0 : 0.75));
            }
        }
    }

    juce::String readStateSchemaAttribute (const juce::MemoryBlock& block)
    {
        const std::unique_ptr<juce::XmlElement> xml (
            juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize())));

        if (xml == nullptr)
            return {};

        return xml->getStringAttribute (OvertureAudioProcessor::stateSchemaAttribute);
    }
}

// T-S1
TEST_CASE ("T-S1: a v0.2.0-shaped state (no new params, no schema attribute) loads with every "
           "v0.3.0 parameter at its neutral default",
           "[state][v030]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Push every new parameter OFF its default first, so "neutral after the
    // load" cannot pass by accident.
    const auto setParam = [&processor] (const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    };

    setParam (ParamIDs::gate, 1.0f);
    setParam (ParamIDs::gateThreshold, -27.5f);
    setParam (ParamIDs::gateRelease, 2.0f);
    setParam (ParamIDs::kneeResponse, 1.0f);
    setParam (ParamIDs::clipQuality, 1.0f);

    const auto blob = makeV020StateBlob (processor.apvts, 220.0f, 19.0f, 71.0f, 55.0f, 66.0f, -33.0f, -4.0f, 88.0f, 1, 2);
    processor.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

    // Exact equality, not Approx: these are defaults, not computed values.
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::gate)->load() == 0.0f);
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::gateThreshold)->load() == -50.0f);
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::gateRelease)->load() == 0.0f);
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::kneeResponse)->load() == 0.0f);
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::clipQuality)->load() == 0.0f);

    // ...and the v0.2.0 values themselves survived.
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::tight)->load() == Catch::Approx (220.0f));
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::drive)->load() == Catch::Approx (19.0f));
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::biteTilt)->load() == Catch::Approx (-33.0f));
    CHECK (static_cast<int> (processor.apvts.getRawParameterValue (ParamIDs::voicing)->load()) == 1);
}

// T-S2
TEST_CASE ("T-S2: a processor restored from v0.2.0-shaped state is BIT-IDENTICAL to one configured "
           "with v0.2.0 semantics, for all three legacy voicings and all oversampling factors",
           "[state][v030][dsp]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    for (int voicingIndex = 0; voicingIndex <= 2; ++voicingIndex)
    {
        for (int oversamplingIndex = 0; oversamplingIndex <= 2; ++oversamplingIndex)
        {
            INFO ("voicing " << voicingIndex << ", oversampling index " << oversamplingIndex);

            OvertureAudioProcessor restored;
            OvertureAudioProcessor reference;

            const auto blob = makeV020StateBlob (restored.apvts, 145.0f, 21.0f, 55.0f, 45.0f, 62.0f,
                                                 -18.0f, -2.5f, 100.0f, voicingIndex, oversamplingIndex);

            restored.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

            // The reference is configured through the parameter API with the
            // same v0.2.0 values and NOTHING touching the v0.3.0 parameters,
            // i.e. exactly what a v0.2.0 build's engine would have seen.
            const auto setParam = [] (OvertureAudioProcessor& processor, const char* id, float realValue)
            {
                auto* param = processor.apvts.getParameter (id);
                REQUIRE (param != nullptr);
                param->setValueNotifyingHost (param->convertTo0to1 (realValue));
            };

            setParam (reference, ParamIDs::tight, 145.0f);
            setParam (reference, ParamIDs::drive, 21.0f);
            setParam (reference, ParamIDs::biteAmount, 55.0f);
            setParam (reference, ParamIDs::kneeSoften, 45.0f);
            setParam (reference, ParamIDs::asymmetryAmount, 62.0f);
            setParam (reference, ParamIDs::biteTilt, -18.0f);
            setParam (reference, ParamIDs::level, -2.5f);
            setParam (reference, ParamIDs::mix, 100.0f);
            setParam (reference, ParamIDs::voicing, static_cast<float> (voicingIndex));
            setParam (reference, ParamIDs::oversampling, static_cast<float> (oversamplingIndex));

            restored.prepareToPlay (sampleRate, blockSize);
            reference.prepareToPlay (sampleRate, blockSize);

            CHECK (restored.getLatencySamples() == reference.getLatencySamples());

            juce::AudioBuffer<float> restoredBuffer (2, blockSize);
            juce::AudioBuffer<float> referenceBuffer (2, blockSize);
            juce::MidiBuffer midi;

            double worstResidual = 0.0;

            for (int block = 0; block < 12; ++block)
            {
                const auto start = static_cast<juce::int64> (block) * blockSize;
                fillTestProgram (restoredBuffer, sampleRate, start);
                fillTestProgram (referenceBuffer, sampleRate, start);

                restored.processBlock (restoredBuffer, midi);
                reference.processBlock (referenceBuffer, midi);

                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < blockSize; ++sample)
                        worstResidual = std::max (worstResidual,
                                                  std::abs (static_cast<double> (restoredBuffer.getReadPointer (channel)[sample])
                                                            - static_cast<double> (referenceBuffer.getReadPointer (channel)[sample])));
            }

            // Bit-identical, not "below a null-test threshold".
            CHECK (worstResidual == 0.0);
        }
    }
}

// T-S4
TEST_CASE ("T-S4: schema-v3 round-trip preserves every new parameter exactly and stamps stateSchema=3",
           "[state][v030]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto setParam = [&processor] (const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    };

    setParam (ParamIDs::gate, 1.0f);
    setParam (ParamIDs::gateThreshold, -37.5f);
    setParam (ParamIDs::gateRelease, 1.0f);
    setParam (ParamIDs::kneeResponse, 1.0f);
    setParam (ParamIDs::clipQuality, 1.0f);
    setParam (ParamIDs::voicing, 3.0f); // Feedback

    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    REQUIRE (saved.getSize() > 0);

    CHECK (readStateSchemaAttribute (saved) == juce::String ("3"));

    // Reset everything back to defaults, then restore.
    for (const char* id : { ParamIDs::gate, ParamIDs::gateThreshold, ParamIDs::gateRelease,
                            ParamIDs::kneeResponse, ParamIDs::clipQuality, ParamIDs::voicing })
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->getDefaultValue());
    }

    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

    CHECK (processor.apvts.getRawParameterValue (ParamIDs::gate)->load() == 1.0f);
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::gateThreshold)->load() == Catch::Approx (-37.5f));
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::gateRelease)->load() == 1.0f);
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::kneeResponse)->load() == 1.0f);
    CHECK (processor.apvts.getRawParameterValue (ParamIDs::clipQuality)->load() == 1.0f);
    CHECK (static_cast<int> (processor.apvts.getRawParameterValue (ParamIDs::voicing)->load())
           == static_cast<int> (ClipperVoicing::feedback));

    // Saving again must keep the stamp (it is written, not merely echoed).
    juce::MemoryBlock resaved;
    processor.getStateInformation (resaved);
    CHECK (readStateSchemaAttribute (resaved) == juce::String ("3"));
}

// T-S5
TEST_CASE ("T-S5: reported latency is unchanged by any v0.3.0 feature - the gate adds zero",
           "[state][v030][latency]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    for (int oversamplingIndex = 0; oversamplingIndex <= 2; ++oversamplingIndex)
    {
        INFO ("oversampling index " << oversamplingIndex);

        OvertureAudioProcessor neutral;
        OvertureAudioProcessor loaded;

        const auto setParam = [] (OvertureAudioProcessor& processor, const char* id, float realValue)
        {
            auto* param = processor.apvts.getParameter (id);
            REQUIRE (param != nullptr);
            param->setValueNotifyingHost (param->convertTo0to1 (realValue));
        };

        setParam (neutral, ParamIDs::oversampling, static_cast<float> (oversamplingIndex));

        setParam (loaded, ParamIDs::oversampling, static_cast<float> (oversamplingIndex));
        setParam (loaded, ParamIDs::gate, 1.0f);
        setParam (loaded, ParamIDs::gateThreshold, -40.0f);
        setParam (loaded, ParamIDs::clipQuality, 1.0f);   // Enhanced (ADAA half-sample delay is NOT reported)
        setParam (loaded, ParamIDs::kneeResponse, 1.0f);
        setParam (loaded, ParamIDs::voicing, 3.0f);       // Feedback

        neutral.prepareToPlay (sampleRate, blockSize);
        loaded.prepareToPlay (sampleRate, blockSize);

        CHECK (loaded.getLatencySamples() == neutral.getLatencySamples());
        CHECK (loaded.getLatencySamples() > 0);
    }
}
