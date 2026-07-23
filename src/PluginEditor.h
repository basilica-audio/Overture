#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "gui/BasilicaLookAndFeel.h"
#include "gui/FilmstripKnob.h"
#include "gui/FilmstripToggle.h"
#include "presets/PresetBar.h"

class OvertureAudioProcessor;

// M3 GUI pilot replication: Overture's photoreal skeuomorphic editor, built
// from the suite-reusable src/gui/ component family (FilmstripKnob,
// FilmstripToggle, BasilicaLookAndFeel - ported verbatim from
// basilica-audio/silentium's M3 pilot) plus the pre-rendered faceplate PNG
// (see .scaffold/gui-assets/faceplate-overture-v1/README.md). Every visible
// control is wired to a real APVTS parameter - no dead decoration, per the
// basilica-gui-design skill's binding spec.
//
// Overture's 11 live parameters are 8 continuous (FilmstripKnob), 1 boolean
// (FilmstripToggle, `bypass`), and 2 discrete choices (`voicing`,
// `oversampling`) - the last pair has no dedicated filmstrip art in this
// asset wave (faceplate-overture-v1/ ships no combo-box asset family), so
// they remain stock juce::ComboBox instances, coloured to match the suite's
// gold-on-gunmetal palette (see PluginEditor.cpp's configureChoice()) rather
// than invented filmstrip art outside the manifest's scope.
//
// Layout: a single per-parameter table (see PluginEditor.cpp) positions
// every control AND its juce::Label caption from the SAME base-resolution
// (900x600 @1x) coordinates the faceplate's four engraved bays
// (input/clipper/output/utility) were authored against - see
// PluginEditorLayout.h.
//
// Window scaling is STEPPED (100/150/200%, a corner control next to the
// preset bar), persisted as a plain property on the APVTS state tree - not a
// free/continuous resize, because the backing art is pre-rendered at fixed
// density tiers (see src/gui/ImageDensity.h).
class OvertureAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OvertureAudioProcessorEditor (OvertureAudioProcessor& processorToEdit);
    ~OvertureAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Which engraved faceplate bay a control belongs to - see
    // PluginEditorLayout.h's inputBay1x/clipperBay1x/outputBay1x/utilityBay1x
    // and faceplate-overture-v1/layout-manifest.json, the ground truth this
    // enum's ordering and every layout table entry (PluginEditor.cpp) must
    // stay faithful to. Public so PluginEditor.cpp's anonymous-namespace
    // layout tables can name it as OvertureAudioProcessorEditor::Bay.
    enum class Bay
    {
        input,
        clipper,
        output,
        utility
    };

    // Public (rather than a private implementation detail) so
    // PluginEditor.cpp's anonymous-namespace layout tables can size their
    // std::array<..., N> against the SAME counts the editor's own member
    // arrays use, instead of a second hand-copied literal that could
    // silently drift out of sync.
    static constexpr int numKnobs = 8;
    static constexpr int numToggles = 1;
    static constexpr int numChoices = 2;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        std::unique_ptr<basilica::gui::FilmstripKnob> slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        std::unique_ptr<basilica::gui::FilmstripToggle> button;
        juce::Label label;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();

    OvertureAudioProcessor& audioProcessor;

    basilica::gui::BasilicaLookAndFeel lookAndFeel;

    juce::Image facePlateImage1x, facePlateImage2x;
    juce::Image brandIconImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    std::array<Knob, numKnobs> knobs;
    std::array<Toggle, numToggles> toggles;
    std::array<Choice, numChoices> choices;

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureAudioProcessorEditor)
};
