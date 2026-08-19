#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/ImageDensity.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

namespace
{
    // Base (@1x, 100% scale) faceplate geometry lives in PluginEditorLayout.h
    // (ovtr::layout) rather than here, so tests/gui/EditorLayoutTests.cpp can
    // assert layout invariants against the exact constants this file lays
    // components out with - see that header's docs.
    using namespace ovtr::layout;
    using Bay = OvertureAudioProcessorEditor::Bay;

    struct KnobLayoutEntry
    {
        const char* parameterId;
        const char* labelText;
        Bay bay;
        int column;
    };

    struct ToggleLayoutEntry
    {
        const char* parameterId;
        const char* labelText;
        Bay bay;
        int column;
    };

    struct ChoiceLayoutEntry
    {
        const char* parameterId;
        const char* labelText;
        Bay bay;
        int column;
    };

    // Signal-flow-grouped, matching faceplate-overture-v1/layout-manifest.json
    // 1:1: input (Tight HPF -> Drive), clipper (voicing + its three v0.2.0
    // shape controls), output (post-clip tilt -> level -> mix), utility
    // (bypass + oversampling). Column order within each bay matches the
    // manifest's own "controls" array order.
    constexpr std::array<KnobLayoutEntry, OvertureAudioProcessorEditor::numKnobs> knobLayout {
        KnobLayoutEntry { ParamIDs::tight, "Tight", Bay::input, 0 },
        KnobLayoutEntry { ParamIDs::drive, "Drive", Bay::input, 1 },
        KnobLayoutEntry { ParamIDs::asymmetryAmount, "Asymmetry", Bay::clipper, 1 },
        KnobLayoutEntry { ParamIDs::kneeSoften, "Knee Soften", Bay::clipper, 2 },
        KnobLayoutEntry { ParamIDs::biteAmount, "Bite", Bay::clipper, 3 },
        KnobLayoutEntry { ParamIDs::biteTilt, "Bite Tilt", Bay::output, 0 },
        KnobLayoutEntry { ParamIDs::level, "Level", Bay::output, 1 },
        KnobLayoutEntry { ParamIDs::mix, "Mix", Bay::output, 2 },
    };

    constexpr std::array<ToggleLayoutEntry, OvertureAudioProcessorEditor::numToggles> toggleLayout {
        ToggleLayoutEntry { ParamIDs::bypass, "Bypass", Bay::utility, 0 },
    };

    constexpr std::array<ChoiceLayoutEntry, OvertureAudioProcessorEditor::numChoices> choiceLayout {
        ChoiceLayoutEntry { ParamIDs::voicing, "Voicing", Bay::clipper, 0 },
        ChoiceLayoutEntry { ParamIDs::oversampling, "Oversampling", Bay::utility, 1 },
    };

    // Total control count per bay (needed to divide each bay's row into
    // equal-width cells) - matches faceplate-overture-v1/layout-manifest.json's
    // "controls" array lengths exactly (2, 4, 3, 2).
    constexpr int bayColumnCount (Bay bay) noexcept
    {
        switch (bay)
        {
            case Bay::input: return 2;
            case Bay::clipper: return 4;
            case Bay::output: return 3;
            case Bay::utility: return 2;
        }

        return 1;
    }

    const juce::Rectangle<int>& bayRect1x (Bay bay) noexcept
    {
        switch (bay)
        {
            case Bay::input: return inputBay1x;
            case Bay::clipper: return clipperBay1x;
            case Bay::output: return outputBay1x;
            case Bay::utility: return utilityBay1x;
        }

        return inputBay1x;
    }

    juce::Image loadImage (const char* data, int size)
    {
        return juce::ImageCache::getFromMemory (data, size);
    }

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs. `presetBar` is a member
    // initialised via the constructor's initialiser list, and its own
    // constructor already calls TRANS() on every button label - member
    // initialisers run in declaration order regardless of the order
    // they're written in, so this helper (called from presetBar's own
    // initialiser expression below) is what actually guarantees
    // installLocalisation() runs before presetBar exists, not an
    // installLocalisation() call in the constructor *body*, which would run
    // too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (OvertureAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state. This ValueTree is
    // exactly what getStateInformation()/setStateInformation() serialise, so
    // a property set here round-trips through host session save/reload the
    // same way the registered parameters do, without needing its own
    // parameter (a scale step is a view choice, not something that should be
    // host-automatable or appear in a DAW's parameter list).
    constexpr const char* uiScaleStepProperty = "uiScaleStep";
}

OvertureAudioProcessorEditor::OvertureAudioProcessorEditor (OvertureAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    setLookAndFeel (&lookAndFeel);

    facePlateImage1x = loadImage (BinaryData::faceplate_overture_900x600_png, BinaryData::faceplate_overture_900x600_pngSize);
    facePlateImage2x = loadImage (BinaryData::faceplate_overture_1800x1200_png, BinaryData::faceplate_overture_1800x1200_pngSize);
    brandIconImage = loadImage (BinaryData::icon256_png, BinaryData::icon256_pngSize);

    // Creation order below doubles as the accessibility/keyboard focus order
    // (JUCE's default FocusTraverser walks children in z-order, i.e.
    // creation order, when no custom traverser is installed) - kept
    // deliberately matching the visual reading order: header/scale control,
    // preset bar, then each bay's controls in signal-flow order (input,
    // clipper, output, utility).
    titleLabel.setText ("Overture", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (juce::Font (juce::FontOptions {}
                                        .withName (juce::Font::getDefaultSerifFontName())
                                        .withHeight (26.0f)
                                        .withStyle ("Bold")));
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (presetBar);

    // A-05-equivalent (see basilica-audio/silentium's M3 a11y review): the
    // scale button's accessible title is set from applyScaleStep() below,
    // which runs once here at construction (with the stored/default step)
    // and again on every subsequent click, so the accessible name always
    // reflects the CURRENT scale instead of a static string that never
    // updates. componentID is set purely so
    // tests/gui/EditorAccessibilityTests.cpp can find this button without
    // depending on its (now dynamic) title.
    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    const auto knobStrip1x = loadImage (BinaryData::knob_brass_strip_160px_128f_png, BinaryData::knob_brass_strip_160px_128f_pngSize);
    const auto knobStrip2x = loadImage (BinaryData::knob_brass_strip_320px_128f_png, BinaryData::knob_brass_strip_320px_128f_pngSize);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<basilica::gui::FilmstripKnob> (knobStrip1x, knobStrip2x, 128);
        configureKnob (knobs[i], entry.parameterId, entry.labelText);
    }

    const auto toggleStrip1x = loadImage (BinaryData::toggle_brass_strip_100px_4f_png, BinaryData::toggle_brass_strip_100px_4f_pngSize);
    const auto toggleStrip2x = loadImage (BinaryData::toggle_brass_strip_200px_4f_png, BinaryData::toggle_brass_strip_200px_4f_pngSize);

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        auto& entry = toggleLayout[i];
        toggles[i].button = std::make_unique<basilica::gui::FilmstripToggle> (entry.labelText, toggleStrip1x, toggleStrip2x);
        configureToggle (toggles[i], entry.parameterId, entry.labelText);
    }

    for (size_t i = 0; i < choiceLayout.size(); ++i)
    {
        auto& entry = choiceLayout[i];
        configureChoice (choices[i], entry.parameterId, entry.labelText);
    }

    setResizable (false, false);

    const auto storedStep = (int) audioProcessor.apvts.state.getProperty (uiScaleStepProperty, 0);
    applyScaleStep (juce::jlimit (0, (int) scaleSteps.size() - 1, storedStep));
}

OvertureAudioProcessorEditor::~OvertureAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void OvertureAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider->setPopupDisplayEnabled (true, true, this);
    knob.slider->setTitle (labelText);
    knob.slider->setName (labelText);
    addAndMakeVisible (*knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        const auto defaultValue = param->getNormalisableRange().convertFrom0to1 (param->getDefaultValue());
        knob.slider->setDoubleClickReturnValue (true, defaultValue);
    }

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (knob.label);

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below, not after: JUCE 8.0.14's SliderParameterAttachment
    // constructor (juce_ParameterAttachments.cpp:128) itself assigns
    // `slider.textFromValueFunction = [&param] (double v) { return
    // param.getText (...); }` (no unit) as part of wiring the attachment -
    // setting our own function BEFORE this point would be silently
    // clobbered the moment the attachment is created (documented JUCE 8.0.14
    // ordering bug, see basilica-audio/silentium's M3 pilot).
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, *knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        // Every parameter declares its unit via .withLabel() in
        // ParameterLayout.cpp (dB/%/Hz), but SliderAttachment's own
        // textFromValueFunction (see above) formats the value but drops the
        // unit entirely. This feeds BOTH the popup value display
        // (setPopupDisplayEnabled above) and the accessibility value string
        // (juce_Slider.cpp:1811's
        // SliderAccessibilityHandler::ValueInterface::getCurrentValueAsString()
        // calls Slider::getTextFromValue(), which calls this same function),
        // so one fix here covers both surfaces. Still uses the parameter's
        // own getText() (not just a raw suffix) so the reported precision/
        // rounding matches what the host itself would display.
        knob.slider->textFromValueFunction = [param] (double v)
        {
            return param->getText (param->convertTo0to1 ((float) v), 0) + " " + param->getLabel();
        };
        knob.slider->updateText();
    }
}

void OvertureAudioProcessorEditor::configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText)
{
    toggle.button->setTitle (labelText);
    toggle.button->setName (labelText);
    addAndMakeVisible (*toggle.button);

    toggle.label.setText (labelText, juce::dontSendNotification);
    toggle.label.setJustificationType (juce::Justification::centredLeft);
    toggle.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (toggle.label);

    toggle.attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, *toggle.button);
}

void OvertureAudioProcessorEditor::configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText)
{
    // No dedicated filmstrip/combo-box art ships in this asset wave (see
    // faceplate-overture-v1/README.md's "11 live controls: ... 2 choices" -
    // the manifest declares the bay slot, not per-control art), so `voicing`
    // and `oversampling` stay stock juce::ComboBox instances, explicitly
    // coloured to match the suite's gold-on-gunmetal palette (the SAME
    // colours BasilicaLookAndFeel's label caption/backing-chip pair uses -
    // see BasilicaLookAndFeel::getLabelTextColour()/
    // getLabelBackingChipColour() - so this box reads as part of the same
    // engraved-gold family rather than a visibly foreign JUCE default).
    choice.box.setColour (juce::ComboBox::backgroundColourId, basilica::gui::BasilicaLookAndFeel::getLabelBackingChipColour());
    choice.box.setColour (juce::ComboBox::textColourId, basilica::gui::BasilicaLookAndFeel::getLabelTextColour());
    choice.box.setColour (juce::ComboBox::outlineColourId, basilica::gui::BasilicaLookAndFeel::getLabelTextColour().withAlpha (0.6f));
    choice.box.setColour (juce::ComboBox::arrowColourId, basilica::gui::BasilicaLookAndFeel::getLabelTextColour());

    choice.box.setTitle (labelText);
    choice.box.setName (labelText);
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
    choice.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice.box);
}

void OvertureAudioProcessorEditor::cycleScale()
{
    applyScaleStep ((scaleStepIndex + 1) % (int) ovtr::layout::scaleSteps.size());
}

void OvertureAudioProcessorEditor::applyScaleStep (int newStepIndex)
{
    using namespace ovtr::layout;

    scaleStepIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, newStepIndex);
    audioProcessor.apvts.state.setProperty (uiScaleStepProperty, scaleStepIndex, nullptr);

    const auto percentText = juce::String ((int) (scaleSteps[(size_t) scaleStepIndex] * 100.0f)) + "%";
    scaleButton.setButtonText (percentText);

    // An explicitly-set AccessibilityHandler title always wins over the
    // button's own text for screen readers (JUCE 8.0.14
    // juce_ButtonAccessibilityHandler.h:67-75), so a title set once at
    // construction and never updated would silently strand AT users on a
    // stale scale forever, with no way to learn the plugin changed. Re-
    // setting the title here, alongside the visible text, on every step
    // change (construction included, since this runs from the constructor
    // too) keeps both surfaces in sync.
    scaleButton.setTitle ("Window scale, " + percentText);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    setSize ((int) std::lround ((float) baseEditorWidth * scale),
             (int) std::lround ((float) baseEditorHeight * scale));
}

void OvertureAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace ovtr::layout;

    g.fillAll (juce::Colours::black);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto plateBounds = juce::Rectangle<float> (0.0f, (float) topStripHeight1x * scale + (float) topStripGap1x * scale,
                                                      (float) plateWidth1x * scale, (float) plateHeight1x * scale);

    const auto& plateImage = basilica::gui::pickImageForWidth (facePlateImage1x, facePlateImage2x,
                                                               plateWidth1x, (int) plateBounds.getWidth());
    if (plateImage.isValid())
        g.drawImage (plateImage, plateBounds);

    if (brandIconImage.isValid())
    {
        const auto d = (float) roundelRadius1x * 1.7f * scale;
        const auto cx = (float) roundelCentre1x.x * scale;
        const auto cy = plateBounds.getY() + (float) roundelCentre1x.y * scale;
        g.drawImage (brandIconImage, juce::Rectangle<float> (d, d).withCentre ({ cx, cy }));
    }
}

void OvertureAudioProcessorEditor::resized()
{
    using namespace ovtr::layout;

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (int v) { return (int) std::lround ((float) v * scale); };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)));
    presetBar.setBounds (topStrip);

    // Everything below is expressed in plate-local coordinates (the base
    // @1x table above), then offset by the top strip + gap and scaled.
    const auto toPlateRect = [&] (juce::Rectangle<int> plateLocal)
    {
        return juce::Rectangle<int> (s (plateLocal.getX()),
                                     s (topStripHeight1x + topStripGap1x) + s (plateLocal.getY()),
                                     s (plateLocal.getWidth()),
                                     s (plateLocal.getHeight()));
    };

    titleLabel.setBounds (toPlateRect (headerBay1x.withWidth (roundelCentre1x.x - headerBay1x.getX() - roundelRadius1x - 8)));

    // Every bay lays its own controls out as one row of equal-width cells,
    // label above/control below - see PluginEditorLayout.h's docs for why
    // this is per-bay rather than one global grid (unlike Silentium's single
    // 5x2 knob grid, Overture's manifest groups controls into four
    // independently-sized bays).
    const auto labelH = s (knobLabelHeight1x);
    const auto knobDiam = s (knobDiameter1x);
    const auto toggleSize = s (toggleSize1x);
    const auto comboBoxH = s (comboBoxHeight1x);

    const auto cellRectFor = [&] (Bay bay, int column)
    {
        const auto bay1x = bayRect1x (bay);
        const auto bayRect = toPlateRect (bay1x);
        const auto cols = bayColumnCount (bay);
        const auto cellW = bayRect.getWidth() / cols;

        return juce::Rectangle<int> (bayRect.getX() + column * cellW, bayRect.getY(), cellW, bayRect.getHeight());
    };

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        const auto cell = cellRectFor (entry.bay, entry.column);

        knobs[i].label.setBounds (cell.getX(), cell.getY(), cell.getWidth(), labelH);
        knobs[i].slider->setBounds (juce::Rectangle<int> (knobDiam, knobDiam)
                                        .withCentre ({ cell.getCentreX(), cell.getY() + labelH + (cell.getHeight() - labelH) / 2 }));
    }

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        auto& entry = toggleLayout[i];
        const auto cell = cellRectFor (entry.bay, entry.column);

        toggles[i].label.setBounds (cell.getX(), cell.getY(), cell.getWidth(), labelH);
        toggles[i].button->setBounds (juce::Rectangle<int> (toggleSize, toggleSize)
                                          .withCentre ({ cell.getCentreX(), cell.getY() + labelH + (cell.getHeight() - labelH) / 2 }));
    }

    for (size_t i = 0; i < choiceLayout.size(); ++i)
    {
        auto& entry = choiceLayout[i];
        const auto cell = cellRectFor (entry.bay, entry.column);
        const auto boxWidth = juce::jmax (s (40), cell.getWidth() - s (8));

        choices[i].label.setBounds (cell.getX(), cell.getY(), cell.getWidth(), labelH);
        choices[i].box.setBounds (juce::Rectangle<int> (boxWidth, comboBoxH)
                                       .withCentre ({ cell.getCentreX(), cell.getY() + labelH + (cell.getHeight() - labelH) / 2 }));
    }
}
