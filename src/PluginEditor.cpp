#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"

namespace
{
    constexpr int knobSize = 100;
    constexpr int textBoxHeight = 20;
    constexpr int labelHeight = 20;
    constexpr int margin = 20;
    constexpr int numKnobs = 5;
    constexpr int choiceBoxHeight = 24;
    constexpr int choiceRowHeight = labelHeight + choiceBoxHeight + margin / 2;
    constexpr int bypassRowHeight = 24;
    constexpr int editorWidth = margin * 2 + numKnobs * knobSize + (numKnobs - 1) * margin;
    constexpr int editorHeight = margin * 2 + bypassRowHeight + margin / 2
                                  + labelHeight + knobSize + textBoxHeight + margin / 2
                                  + choiceRowHeight;
}

OvertureAudioProcessorEditor::OvertureAudioProcessorEditor (OvertureAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit)
{
    configureKnob (tightKnob, ParamIDs::tight, "Tight");
    configureKnob (driveKnob, ParamIDs::drive, "Drive");
    configureKnob (toneKnob, ParamIDs::tone, "Tone");
    configureKnob (levelKnob, ParamIDs::level, "Level");
    configureKnob (mixKnob, ParamIDs::mix, "Mix");

    configureChoice (voicingChoice, ParamIDs::voicing, "Voicing");
    configureChoice (oversamplingChoice, ParamIDs::oversampling, "Oversampling");

    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::bypass, bypassButton);

    setResizable (false, false);
    setSize (editorWidth, editorHeight);
}

OvertureAudioProcessorEditor::~OvertureAudioProcessorEditor() = default;

void OvertureAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, textBoxHeight);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    // false => label sits above the slider it tracks; JUCE repositions it
    // automatically whenever the slider's bounds change, so resized() only
    // needs to place the sliders themselves.
    knob.label.attachToComponent (&knob.slider, false);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob.slider);
}

void OvertureAudioProcessorEditor::configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText)
{
    addAndMakeVisible (choice.box);

    // ComboBoxAttachment does not populate the box itself (see its JUCE doc
    // comment); pull the choice strings straight from the live APVTS
    // parameter (AudioParameterChoice::getAllValueStrings() returns its
    // `choices` array) rather than duplicating the string list here, so the
    // GUI can never drift out of sync with ParameterLayout.cpp. Item IDs are
    // 1-based to match ComboBox's convention; ComboBoxAttachment maps them
    // back to the parameter's 0-based choice index.
    if (auto* parameter = audioProcessor.apvts.getParameter (parameterId))
        choice.box.addItemList (parameter->getAllValueStrings(), 1);

    choice.label.setText (labelText, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    choice.label.attachToComponent (&choice.box, false);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice.box);
}

void OvertureAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    auto bypassRow = bounds.removeFromTop (bypassRowHeight);
    bypassButton.setBounds (bypassRow.removeFromLeft (100));

    bounds.removeFromTop (margin / 2);

    auto knobRow = bounds.removeFromTop (labelHeight + knobSize + textBoxHeight);
    knobRow.removeFromTop (labelHeight); // room for the attached labels above each knob

    const auto slotWidth = knobRow.getWidth() / numKnobs;

    for (auto* knob : { &tightKnob, &driveKnob, &toneKnob, &levelKnob, &mixKnob })
        knob->slider.setBounds (knobRow.removeFromLeft (slotWidth).reduced (margin / 2, 0));

    bounds.removeFromTop (margin / 2);

    auto choiceRow = bounds.removeFromTop (choiceRowHeight);
    choiceRow.removeFromTop (labelHeight); // room for the attached labels above each combo box

    const auto choiceSlotWidth = choiceRow.getWidth() / 2;
    voicingChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
    oversamplingChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
}
