#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    // Convenience wrapper: fetches a parameter by ID and requires it to
    // exist before returning, so every SECTION below fails loudly (not with
    // a null-deref) if an ID typo ever creeps in.
    juce::RangedAudioParameter* requireParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param;
    }

    // Checks that a float parameter's underlying NormalisableRange covers
    // [expectedMin, expectedMax], independent of any skew/log mapping.
    void checkFloatRange (juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& id,
                           float expectedMin,
                           float expectedMax)
    {
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id));
        REQUIRE (param != nullptr);

        const auto range = param->getNormalisableRange().getRange();
        CHECK (range.getStart() == Catch::Approx (expectedMin));
        CHECK (range.getEnd() == Catch::Approx (expectedMax));
    }

    // Checks a float parameter's default value in real (non-normalised)
    // units, going through convertTo0to1 so log-skewed ranges are handled
    // the same way as linear ones.
    void checkFloatDefault (juce::AudioProcessorValueTreeState& apvts,
                             const juce::String& id,
                             float expectedDefault)
    {
        auto* param = requireParam (apvts, id);
        CHECK (param->getDefaultValue() == Catch::Approx (param->convertTo0to1 (expectedDefault)).margin (1e-4));
    }
}

TEST_CASE ("Processor instantiates with the expected parameters", "[processor][parameters]")
{
    OvertureAudioProcessor processor;
    auto& apvts = processor.apvts;

    SECTION ("plugin name")
    {
        CHECK (processor.getName() == juce::String ("Overture"));
    }

    SECTION ("all documented parameter IDs resolve")
    {
        static constexpr const char* allIds[] = {
            ParamIDs::tight, ParamIDs::drive, ParamIDs::biteAmount, ParamIDs::kneeSoften,
            ParamIDs::asymmetryAmount, ParamIDs::biteTilt, ParamIDs::level, ParamIDs::mix,
            ParamIDs::bypass, ParamIDs::voicing, ParamIDs::oversampling,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("ParamIDs::tone is retired - not a live/registered parameter as of v0.2.0")
    {
        CHECK (apvts.getParameter (ParamIDs::tone) == nullptr);
    }

    SECTION ("total parameter count matches the v0.2.0 layout")
    {
        // v0.1.0 had 8; v0.2.0 removes `tone` and adds `biteAmount`,
        // `kneeSoften`, `asymmetryAmount`, `biteTilt` -> 8 - 1 + 4 = 11.
        CHECK (apvts.processor.getParameters().size() == 11);
    }

    SECTION ("Tight: high-pass pre-emphasis defaults and range (v0.2.0: default 130 -> 100 Hz)")
    {
        checkFloatDefault (apvts, ParamIDs::tight, 100.0f);
        checkFloatRange (apvts, ParamIDs::tight, 20.0f, 400.0f);
    }

    SECTION ("Drive: clipper input gain defaults and range (v0.2.0: default 8 -> 3 dB)")
    {
        checkFloatDefault (apvts, ParamIDs::drive, 3.0f);
        checkFloatRange (apvts, ParamIDs::drive, 0.0f, 40.0f);
    }

    SECTION ("Bite: frequency-dependent clipper gain defaults and range (new in v0.2.0)")
    {
        checkFloatDefault (apvts, ParamIDs::biteAmount, 65.0f);
        checkFloatRange (apvts, ParamIDs::biteAmount, 0.0f, 100.0f);
    }

    SECTION ("Knee Soften: drive-dependent knee softening defaults and range (new in v0.2.0)")
    {
        checkFloatDefault (apvts, ParamIDs::kneeSoften, 40.0f);
        checkFloatRange (apvts, ParamIDs::kneeSoften, 0.0f, 100.0f);
    }

    SECTION ("Asymmetry: exposed clipper bias defaults and range (new in v0.2.0)")
    {
        checkFloatDefault (apvts, ParamIDs::asymmetryAmount, 40.0f);
        checkFloatRange (apvts, ParamIDs::asymmetryAmount, 0.0f, 100.0f);
    }

    SECTION ("Bite Tilt: post-clip bidirectional tilt defaults and range (new in v0.2.0, replaces Tone)")
    {
        checkFloatDefault (apvts, ParamIDs::biteTilt, 0.0f);
        checkFloatRange (apvts, ParamIDs::biteTilt, -100.0f, 100.0f);
    }

    SECTION ("Level: output trim defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::level, 0.0f);
        checkFloatRange (apvts, ParamIDs::level, -24.0f, 24.0f);
    }

    SECTION ("Mix: dry/wet defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::mix, 100.0f);
        checkFloatRange (apvts, ParamIDs::mix, 0.0f, 100.0f);
    }

    SECTION ("Bypass: defaults to off")
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::bypass));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }

    SECTION ("Voicing: three choices, defaults to Asymmetric, indices frozen")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::voicing));
        REQUIRE (param != nullptr);
        CHECK (param->choices.size() == 3);
        CHECK (param->getIndex() == 0);
        CHECK (param->getCurrentChoiceName() == juce::String ("Asymmetric"));
        CHECK (param->choices[0] == juce::String ("Asymmetric"));
        CHECK (param->choices[1] == juce::String ("Soft Symmetric"));
        CHECK (param->choices[2] == juce::String ("Hard Clip"));
    }

    SECTION ("Oversampling: three choices, defaults to 4x")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::oversampling));
        REQUIRE (param != nullptr);
        CHECK (param->choices.size() == 3);
        CHECK (param->getIndex() == 1);
        CHECK (param->getCurrentChoiceName() == juce::String ("4x"));
    }
}

TEST_CASE ("getBypassParameter() returns the host-visible Bypass parameter", "[processor][parameters][bypass]")
{
    OvertureAudioProcessor processor;

    auto* bypassParam = processor.getBypassParameter();
    REQUIRE (bypassParam != nullptr);
    CHECK (bypassParam == processor.apvts.getParameter (ParamIDs::bypass));
}
