#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

namespace
{
    constexpr int knobSize = 90;
    constexpr int textBoxHeight = 20;
    constexpr int labelHeight = 20;
    constexpr int margin = 20;
    constexpr int numKnobs = 8;
    constexpr int choiceBoxHeight = 24;
    constexpr int choiceRowHeight = labelHeight + choiceBoxHeight + margin / 2;
    constexpr int bypassRowHeight = 24;
    constexpr int presetBarHeight = 28;
    constexpr int editorWidth = margin * 2 + numKnobs * knobSize + (numKnobs - 1) * (margin / 2);
    constexpr int editorHeight = margin * 2 + presetBarHeight + margin / 2 + bypassRowHeight + margin / 2
                                  + labelHeight + knobSize + textBoxHeight + margin / 2
                                  + choiceRowHeight;

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs. `presetBar` is a member
    // initialised via the constructor's initialiser list, and its own
    // constructor already calls TRANS() on every button label - member
    // initialisers run in declaration order regardless of the order they're
    // written in, so this helper (called from presetBar's own initialiser
    // expression below) is what actually guarantees installLocalisation()
    // runs before presetBar exists, not an installLocalisation() call in
    // the constructor *body*, which would run too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (OvertureAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }
}

OvertureAudioProcessorEditor::OvertureAudioProcessorEditor (OvertureAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    addAndMakeVisible (presetBar);

    configureKnob (tightKnob, ParamIDs::tight, "Tight");
    configureKnob (driveKnob, ParamIDs::drive, "Drive");
    configureKnob (biteKnob, ParamIDs::biteAmount, "Bite");
    configureKnob (kneeSoftenKnob, ParamIDs::kneeSoften, "Knee Soften");
    configureKnob (asymmetryKnob, ParamIDs::asymmetryAmount, "Asymmetry");
    configureKnob (biteTiltKnob, ParamIDs::biteTilt, "Bite Tilt");
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

    // Parameter names are core/DSP terminology, never translated (M2 i18n
    // spec: "NEVER translate core/DSP terminology" - see
    // src/presets/Localisation.h's docs) - deliberately not TRANS()'d.
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

    // Parameter names are core/DSP terminology, never translated - see
    // configureKnob()'s docs above.
    choice.label.setText (labelText, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    choice.label.attachToComponent (&choice.box, false);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice.box);
}

void OvertureAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    presetBar.setBounds (bounds.removeFromTop (presetBarHeight));
    bounds.removeFromTop (margin / 2);

    auto bypassRow = bounds.removeFromTop (bypassRowHeight);
    bypassButton.setBounds (bypassRow.removeFromLeft (100));

    bounds.removeFromTop (margin / 2);

    auto knobRow = bounds.removeFromTop (labelHeight + knobSize + textBoxHeight);
    knobRow.removeFromTop (labelHeight); // room for the attached labels above each knob

    const auto slotWidth = knobRow.getWidth() / numKnobs;

    for (auto* knob : { &tightKnob, &driveKnob, &biteKnob, &kneeSoftenKnob, &asymmetryKnob, &biteTiltKnob, &levelKnob, &mixKnob })
        knob->slider.setBounds (knobRow.removeFromLeft (slotWidth).reduced (margin / 4, 0));

    bounds.removeFromTop (margin / 2);

    auto choiceRow = bounds.removeFromTop (choiceRowHeight);
    choiceRow.removeFromTop (labelHeight); // room for the attached labels above each combo box

    const auto choiceSlotWidth = choiceRow.getWidth() / 2;
    voicingChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
    oversamplingChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
}
