#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Accessibility tests for the M3 photoreal editor, ported from
// basilica-audio/silentium's M3 a11y review follow-up (A-01/A-02/A-05):
// assert the actual AccessibilityHandler-level behaviour, not just that the
// editor constructs without crashing (EditorSnapshotTests.cpp already covers
// that). juce::ScopedJuceInitialiser_GUI is installed once for the whole
// test binary in tests/TestMain.cpp, so constructing Components is safe here
// even though this is a headless console executable with no running message
// loop or native window/peer.
//
// Deliberately calls createAccessibilityHandler() directly rather than the
// more commonly used getAccessibilityHandler(): the latter (JUCE 8.0.14
// juce_Component.cpp:3323-3326) only returns a handler once the component
// has a live native window peer (getWindowHandle() != nullptr), which this
// headless, no-message-loop test binary never has. createAccessibilityHandler()
// is public API specifically meant to be safely callable/overridable
// independent of any live OS accessibility bridge (see its own docs in
// juce_Component.h) - callers a step removed from the OS bridge (like this
// test) are exactly the documented exception to "should rarely be called
// directly".
namespace
{
    // All FilmstripKnob/FilmstripToggle/ComboBox instances and the scale
    // button are direct children of the editor itself (see
    // PluginEditor.cpp's addAndMakeVisible calls - none of them live inside
    // a further nested sub-container), so a flat (non-recursive) scan of
    // direct children is sufficient and avoids needing any additional
    // test-only accessors on the editor.
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

    // juce::Button::createAccessibilityHandler() (unlike juce::Slider's and
    // juce::ComboBox's) is declared PROTECTED (JUCE 8.0.14 juce_Button.h) -
    // calling it through a FilmstripToggle*/juce::Button* would fail to
    // compile even though it's the exact same public virtual originally
    // declared on juce::Component. Per the C++ standard's
    // access-control-for-virtual-calls rule ([class.access.virt]), access is
    // checked against the STATIC type used to name the call, not the dynamic
    // override - calling through a juce::Component& (where the function is
    // public) compiles, and virtual dispatch still correctly invokes the
    // most-derived override at runtime. Used uniformly for all component
    // types tested here for consistency.
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
        const char* label;
        const char* unitSuffix;
    };

    // One representative knob per unit declared in ParameterLayout.cpp
    // (.withLabel("dB"/"Hz"/"%")) - the gap this guards against is units
    // being dropped entirely, not a per-parameter formatting detail, so this
    // doesn't need to be exhaustive over all 8 knobs to catch a regression.
    const Expectation expectations[] = {
        { "Tight", "Hz" },
        { "Drive", "dB" },
        { "Bite", "%" },
    };

    for (const auto& expectation : expectations)
    {
        auto* knob = findChildByTitle<basilica::gui::FilmstripKnob> (editor, expectation.label);
        REQUIRE (knob != nullptr);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.label << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.endsWith (expectation.unitSuffix));
    }
}

TEST_CASE ("Toggle accessible name matches its visual label and exposes a checkable state", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    auto* toggle = findChildByTitle<basilica::gui::FilmstripToggle> (editor, "Bypass");
    REQUIRE (toggle != nullptr);
    CHECK (toggle->getTitle() == "Bypass");

    const auto handler = createHandlerForTest (*toggle);
    REQUIRE (handler != nullptr);

    // FilmstripToggle calls setClickingTogglesState(true) (FilmstripToggle.cpp),
    // so juce::Button::isToggleable() is true and the base juce::Button
    // AccessibilityHandler correctly exposes checkable/checked state.
    CHECK (handler->getCurrentState().isCheckable());
}

TEST_CASE ("Choice combo boxes expose their parameter's accessible name and full option list", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* label;
        int numOptions;
    };

    // Both discrete parameters (src/params/ParameterLayout.cpp): Voicing has
    // 4 choices (Asymmetric/Soft Symmetric/Hard Clip/Feedback - the fourth
    // entry landed with v0.3.0's circuit-solved feedback clipper), and
    // Oversampling has 3 (2x/4x/8x).
    const Expectation expectations[] = {
        { "Voicing", 4 },
        { "Oversampling", 3 },
    };

    for (const auto& expectation : expectations)
    {
        auto* box = findChildByTitle<juce::ComboBox> (editor, expectation.label);
        REQUIRE (box != nullptr);
        CHECK (box->getNumItems() == expectation.numOptions);

        const auto handler = createHandlerForTest (*box);
        REQUIRE (handler != nullptr);
        CHECK (handler->getRole() == juce::AccessibilityRole::comboBox);
    }
}

TEST_CASE ("Scale button's accessible title reflects the current scale percentage, not a static string", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    auto* scaleButton = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("scaleButton"));
    REQUIRE (scaleButton != nullptr);

    // At construction (100% step), the title must already contain the
    // CURRENT percentage, not a static string that never updated.
    CHECK (scaleButton->getTitle().contains ("100%"));

    // Cycle the scale via the SAME onClick callback a mouse/keyboard/AT
    // click would invoke (PluginEditor.cpp wires scaleButton.onClick to
    // cycleScale()) - called directly rather than via triggerClick(), which
    // only posts an async command message (JUCE 8.0.14
    // juce_Button.cpp:359-362) that would need a running message loop to
    // ever actually fire, which this headless test binary doesn't have.
    REQUIRE (scaleButton->onClick);
    scaleButton->onClick();

    CHECK (scaleButton->getButtonText() == "150%");
    CHECK (scaleButton->getTitle().contains ("150%"));
    CHECK_FALSE (scaleButton->getTitle().contains ("100%"));
}

// A11y suite pattern (Silentium#5 wave, 2026-08): juce::Slider ships with
// setWantsKeyboardFocus(false) in JUCE 8.0.14 (juce_Slider.cpp:1461,
// Slider::init), so FilmstripKnob was silently unreachable by Tab and its
// keyPressed()/focus ring never fired - and even when focused, the base
// keyPressed (juce_Slider.cpp:1029) steps by the raw parameter interval
// (0.01 dB on Drive's 40 dB range) and ignores Shift entirely. These tests
// pin the fixed contract (setWantsKeyboardFocus(true) + KeyboardSteps.h).

TEST_CASE ("Every interactive control is keyboard-focusable", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    int knobsSeen = 0, togglesSeen = 0, combosSeen = 0;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        auto* child = editor.getChildComponent (i);

        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            ++knobsSeen;
            INFO ("knob \"" << slider->getTitle().toStdString() << "\"");
            CHECK (slider->getWantsKeyboardFocus());
        }
        else if (auto* toggle = dynamic_cast<juce::Button*> (child))
        {
            if (child->getComponentID() != "scaleButton")
                ++togglesSeen;
            CHECK (toggle->getWantsKeyboardFocus());
        }
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (child))
        {
            ++combosSeen;
            CHECK (combo->getWantsKeyboardFocus());
        }
    }

    // All 8 knobs, the Bypass toggle and both choice combos must be present
    // AND focusable - a zero-match loop must not pass vacuously.
    CHECK (knobsSeen == 8);
    CHECK (togglesSeen == 1);
    CHECK (combosSeen == 2);

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
    // base-class step would be 0.01 dB (4000 presses per sweep).
    auto* knob = findChildByTitle<basilica::gui::FilmstripKnob> (editor, "Drive");
    REQUIRE (knob != nullptr);

    knob->setValue (20.0, juce::sendNotificationSync);

    // Called through Component& for the same [class.access.virt] reason
    // documented on createHandlerForTest().
    juce::Component& knobAsComponent = *knob;

    // Plain Right = 1% of the 40 dB range = 0.4 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (20.4).margin (1.0e-4));

    // Shift+Right = 0.1% = 0.04 dB (the keyboard analog of Shift-drag).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                          juce::ModifierKeys::shiftModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (20.44).margin (1.0e-4));

    // Plain Left steps back down symmetrically.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (20.04).margin (1.0e-4));

    // PageDown = 10% = 4 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey)));
    CHECK (knob->getValue() == Catch::Approx (16.04).margin (1.0e-4));

    // Home/End jump to the range extremes (WAI-ARIA slider pattern).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (knob->getValue() == Catch::Approx (0.0).margin (1.0e-4));
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (knob->getValue() == Catch::Approx (40.0).margin (1.0e-4));

    // Ctrl/Cmd-modified presses are host shortcuts - never consumed.
    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                              juce::ModifierKeys::ctrlModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (40.0).margin (1.0e-4));
}

TEST_CASE ("Bypass toggle reports the toggle-button accessibility role, not plain button", "[gui][a11y]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    auto* toggle = findChildByTitle<basilica::gui::FilmstripToggle> (editor, "Bypass");
    REQUIRE (toggle != nullptr);

    const auto handler = createHandlerForTest (*toggle);
    REQUIRE (handler != nullptr);

    // FilmstripToggle now derives from juce::ToggleButton, whose
    // createAccessibilityHandler reports AccessibilityRole::toggleButton
    // (JUCE 8.0.14, juce_ToggleButton.cpp:71) - a plain juce::Button
    // subclass reports only AccessibilityRole::button, which mis-files the
    // control in VoiceOver's rotor / NVDA's by-type navigation.
    CHECK (handler->getRole() == juce::AccessibilityRole::toggleButton);
    CHECK (handler->getCurrentState().isCheckable());
}
