#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
