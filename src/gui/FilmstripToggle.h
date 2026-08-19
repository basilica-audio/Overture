#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable toggle switch backed by a 4-frame Blender filmstrip (see
// .scaffold/gui-assets/toggle-brass-v1/README.md): off / on / off+hover /
// on+hover, in that frame order (frame 0 at the top of the strip).
namespace basilica::gui
{
    // Derives from juce::ToggleButton (not juce::Button) so the accessibility
    // handler reports AccessibilityRole::toggleButton (JUCE 8.0.14,
    // juce_ToggleButton.cpp:71) - a plain juce::Button subclass reports only
    // AccessibilityRole::button (juce_Button.cpp:725-727), which mis-files
    // the control in VoiceOver's rotor / NVDA's by-type quick-nav even
    // though the checkable/checked STATE already worked via
    // setClickingTogglesState. paintButton() is a full custom override, so
    // no ToggleButton default drawing leaks through.
    class FilmstripToggle : public juce::ToggleButton
    {
    public:
        FilmstripToggle (const juce::String& buttonName, juce::Image strip1x, juce::Image strip2x);
        ~FilmstripToggle() override;

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        // Frame index for a given (hovered, isOn) state pair, derived from
        // the asset's own frame TABLE (toggle-brass-v1/README.md):
        //   0 = off,          1 = on,
        //   2 = off + hover,  3 = on + hover
        // i.e. index = (hovered ? 2 : 0) + (isOn ? 1 : 0). Note the README's
        // prose formula ("state * 2 + hovered") does not actually reproduce
        // its own table and should not be used verbatim - this method is the
        // verified mapping, exposed for unit testing (see
        // tests/gui/FilmstripFrameMathTests.cpp).
        static int frameIndexFor (bool isOn, bool isHovered) noexcept;

    private:
        const juce::Image& imageForCurrentWidth() const noexcept;

        juce::Image strip1x, strip2x;
        static constexpr int numFrames = 4;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilmstripToggle)
    };
}
