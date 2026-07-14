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
    auto* toneParam = processor.apvts.getParameter (ParamIDs::tone);
    auto* levelParam = processor.apvts.getParameter (ParamIDs::level);
    auto* mixParam = processor.apvts.getParameter (ParamIDs::mix);
    auto* bypassParam = processor.apvts.getParameter (ParamIDs::bypass);
    auto* voicingParam = processor.apvts.getParameter (ParamIDs::voicing);
    auto* oversamplingParam = processor.apvts.getParameter (ParamIDs::oversampling);

    REQUIRE (tightParam != nullptr);
    REQUIRE (driveParam != nullptr);
    REQUIRE (toneParam != nullptr);
    REQUIRE (levelParam != nullptr);
    REQUIRE (mixParam != nullptr);
    REQUIRE (bypassParam != nullptr);
    REQUIRE (voicingParam != nullptr);
    REQUIRE (oversamplingParam != nullptr);

    tightParam->setValueNotifyingHost (tightParam->convertTo0to1 (250.0f));
    driveParam->setValueNotifyingHost (driveParam->convertTo0to1 (33.0f));
    toneParam->setValueNotifyingHost (toneParam->convertTo0to1 (3500.0f));
    levelParam->setValueNotifyingHost (levelParam->convertTo0to1 (-6.5f));
    mixParam->setValueNotifyingHost (mixParam->convertTo0to1 (42.0f));
    bypassParam->setValueNotifyingHost (1.0f); // non-default: on
    voicingParam->setValueNotifyingHost (voicingParam->convertTo0to1 (2.0f)); // "Hard Clip"
    oversamplingParam->setValueNotifyingHost (oversamplingParam->convertTo0to1 (2.0f)); // "8x"

    const auto savedTight = tightParam->getValue();
    const auto savedDrive = driveParam->getValue();
    const auto savedTone = toneParam->getValue();
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
    toneParam->setValueNotifyingHost (toneParam->getDefaultValue());
    levelParam->setValueNotifyingHost (levelParam->getDefaultValue());
    mixParam->setValueNotifyingHost (mixParam->getDefaultValue());
    bypassParam->setValueNotifyingHost (bypassParam->getDefaultValue());
    voicingParam->setValueNotifyingHost (voicingParam->getDefaultValue());
    oversamplingParam->setValueNotifyingHost (oversamplingParam->getDefaultValue());

    REQUIRE (tightParam->getValue() != Catch::Approx (savedTight));
    REQUIRE (driveParam->getValue() != Catch::Approx (savedDrive));
    REQUIRE (toneParam->getValue() != Catch::Approx (savedTone));
    REQUIRE (levelParam->getValue() != Catch::Approx (savedLevel));
    REQUIRE (mixParam->getValue() != Catch::Approx (savedMix));
    REQUIRE (bypassParam->getValue() != Catch::Approx (savedBypass));
    REQUIRE (voicingParam->getValue() != Catch::Approx (savedVoicing));
    REQUIRE (oversamplingParam->getValue() != Catch::Approx (savedOversampling));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (tightParam->getValue() == Catch::Approx (savedTight).margin (1e-6));
    CHECK (driveParam->getValue() == Catch::Approx (savedDrive).margin (1e-6));
    CHECK (toneParam->getValue() == Catch::Approx (savedTone).margin (1e-6));
    CHECK (levelParam->getValue() == Catch::Approx (savedLevel).margin (1e-6));
    CHECK (mixParam->getValue() == Catch::Approx (savedMix).margin (1e-6));
    CHECK (bypassParam->getValue() == Catch::Approx (savedBypass).margin (1e-6));
    CHECK (voicingParam->getValue() == Catch::Approx (savedVoicing).margin (1e-6));
    CHECK (oversamplingParam->getValue() == Catch::Approx (savedOversampling).margin (1e-6));
}
