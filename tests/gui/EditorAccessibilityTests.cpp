#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/MasterCropKnob.h"
#include "gui/SpriteToggle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Accessibility tests for the wave-3 compositional editor, carrying over
// the suite's M3 a11y review contract (A-01/A-02/A-05/A-09/A-10): assert
// the actual AccessibilityHandler-level behaviour, not just that the
// editor constructs. juce::ScopedJuceInitialiser_GUI is installed once for
// the whole test binary in tests/TestMain.cpp.
//
// createAccessibilityHandler() is called directly rather than
// getAccessibilityHandler(): the latter (JUCE 8.0.14
// juce_Component.cpp:3323-3326) only returns a handler once the component
// has a live native window peer, which this headless test binary never
// has.
namespace
{
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;
        }

        return nullptr;
    }

    // juce::Button::createAccessibilityHandler() is declared PROTECTED
    // (JUCE 8.0.14 juce_Button.h) - calling through a juce::Component&
    // (where the virtual is public) compiles and still dispatches to the
    // most-derived override ([class.access.virt]).
    std::unique_ptr<juce::AccessibilityHandler> createHandlerForTest (juce::Component& component)
    {
        return component.createAccessibilityHandler();
    }
}

TEST_CASE ("Knob accessibility value strings include their declared unit", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* title;
        const char* unitSuffix;
    };

    // One representative knob per unit declared in ParameterLayout.cpp
    // (.withLabel("dB"/"Hz"/"%")).
    const Expectation expectations[] = {
        { "Tight", "Hz" },
        { "Drive", "dB" },
        { "Bite", "%" },
    };

    for (const auto& expectation : expectations)
    {
        auto* knob = findChildByTitle<basilica::gui::MasterCropKnob> (editor, expectation.title);
        REQUIRE (knob != nullptr);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.title << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.endsWith (expectation.unitSuffix));
    }
}

TEST_CASE ("Selector knobs announce their choice NAME, not a bare index", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* title;
        const char* parameterId;
        const char* defaultChoiceName;
    };

    // Both discrete parameters (ParameterLayout.cpp): Voicing has 4
    // choices (default Asymmetric), Oversampling has 3 (default 4x).
    const Expectation expectations[] = {
        { "Voicing", "voicing", "Asymmetric" },
        { "Oversampling", "oversampling", "4x" },
    };

    for (const auto& expectation : expectations)
    {
        auto* selector = findChildByTitle<basilica::gui::MasterCropKnob> (editor, expectation.title);
        REQUIRE (selector != nullptr);

        const auto handler = createHandlerForTest (*selector);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("selector \"" << expectation.title << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.contains (expectation.defaultChoiceName));

        // The stepped selector really snaps: its slider interval is 1.
        CHECK (selector->getInterval() == Catch::Approx (1.0));
    }
}

TEST_CASE ("Toggle accessible name matches its parameter and exposes a checkable state", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    auto* toggle = findChildByTitle<basilica::gui::SpriteToggle> (editor, "Bypass");
    REQUIRE (toggle != nullptr);
    CHECK (toggle->getTitle() == "Bypass");

    const auto handler = createHandlerForTest (*toggle);
    REQUIRE (handler != nullptr);

    // SpriteToggle calls setClickingTogglesState(true), so juce::Button's
    // AccessibilityHandler exposes checkable/checked state.
    CHECK (handler->getCurrentState().isCheckable());
}

TEST_CASE ("Scale button's accessible title reflects the current scale percentage, not a static string", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    auto* scaleButton = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("scaleButton"));
    REQUIRE (scaleButton != nullptr);

    CHECK (scaleButton->getTitle().contains ("100%"));

    // Cycle via the SAME onClick callback a real click would invoke -
    // triggerClick() only posts an async message needing a message loop.
    REQUIRE (scaleButton->onClick);
    scaleButton->onClick();

    CHECK (scaleButton->getButtonText() == "150%");
    CHECK (scaleButton->getTitle().contains ("150%"));
    CHECK_FALSE (scaleButton->getTitle().contains ("100%"));
}

TEST_CASE ("Every interactive control is keyboard-focusable", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    int slidersSeen = 0, togglesSeen = 0;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        auto* child = editor.getChildComponent (i);

        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            ++slidersSeen;
            INFO ("slider \"" << slider->getTitle().toStdString() << "\"");
            CHECK (slider->getWantsKeyboardFocus());
        }
        else if (auto* toggle = dynamic_cast<basilica::gui::SpriteToggle*> (child))
        {
            ++togglesSeen;
            CHECK (toggle->getWantsKeyboardFocus());
        }
    }

    // All 8 knobs + both stepped selectors are sliders; the Bypass lever
    // is the one SpriteToggle. A zero-match loop must not pass vacuously.
    CHECK (slidersSeen == 10);
    CHECK (togglesSeen == 1);

    auto* scaleButton = editor.findChildWithID ("scaleButton");
    REQUIRE (scaleButton != nullptr);
    CHECK (scaleButton->getWantsKeyboardFocus());
}

TEST_CASE ("Arrow keys step knobs by a practical amount, Shift+Arrow steps finer", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    // Drive: linear 0..40 dB, 0.01 dB interval (ParameterLayout.cpp) - the
    // JUCE base-class step would be 0.01 dB (4000 presses per sweep).
    auto* knob = findChildByTitle<basilica::gui::MasterCropKnob> (editor, "Drive");
    REQUIRE (knob != nullptr);

    const auto range = knob->getMaximum() - knob->getMinimum();
    knob->setValue (knob->getMinimum() + range * 0.5, juce::dontSendNotification);
    const auto before = knob->getValue();

    REQUIRE (knob->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    const auto coarseStep = knob->getValue() - before;

    // WAI-ARIA slider pattern: Arrow ~1% of the range.
    CHECK (coarseStep > range * 0.005);
    CHECK (coarseStep < range * 0.02);

    const auto beforeFine = knob->getValue();
    REQUIRE (knob->keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                               juce::ModifierKeys::shiftModifier, 0)));
    const auto fineStep = knob->getValue() - beforeFine;

    CHECK (fineStep > 0.0);
    CHECK (fineStep < coarseStep);
}
