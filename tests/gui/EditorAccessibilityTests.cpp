#include "PluginEditor.h"
#include "PluginProcessor.h"

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
