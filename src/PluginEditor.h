#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class OvertureAudioProcessor;

// A simple, functional v0.1 editor: one rotary slider per continuous
// parameter, bound to the APVTS via SliderAttachment, plus a bypass toggle
// and two combo boxes for the discrete Voicing/Oversampling choices. A
// custom vector-drawn GUI is a later milestone (M3); this is deliberately
// plain but fully wired and usable - every automatable parameter has a
// working control.
class OvertureAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OvertureAudioProcessorEditor (OvertureAudioProcessor& processorToEdit);
    ~OvertureAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // One knob + label per continuous parameter, in signal-flow order.
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // One combo box + label per discrete (choice) parameter.
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText);

    OvertureAudioProcessor& audioProcessor;

    Knob tightKnob;
    Knob driveKnob;
    Knob toneKnob;
    Knob levelKnob;
    Knob mixKnob;

    Choice voicingChoice;
    Choice oversamplingChoice;

    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureAudioProcessorEditor)
};
