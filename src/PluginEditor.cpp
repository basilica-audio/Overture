#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

#include <cmath>

namespace
{
    using namespace ovtr::layout;

    juce::Image loadImage (const char* data, int size)
    {
        return juce::ImageCache::getFromMemory (data, size);
    }

    // M2 i18n frame: selects German (resources/i18n/de.txt) or falls
    // through to English, once, at editor construction - see
    // Localisation.h's docs. Called from presetBar's own initialiser
    // expression so installLocalisation() is guaranteed to run before
    // PresetBar's constructor TRANS()es its button labels.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (OvertureAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state.
    constexpr const char* uiScaleStepProperty = "uiScaleStep";

    // Engraved lettering: gilded antique gold with a dark drop shadow one
    // scaled pixel below - the correct read for light-on-dark lettering on
    // the near-black basalt plate (the same gilded convention apotheosis'
    // typography pass established for dark grounds; a dark-ink engraving
    // would vanish here).
    const basilica::gui::EngravedTextStyle plateLabelStyle {
        juce::Colour (0xf0d6ad5e), juce::Colour (0x8c000000), 13.0f, 0.16f, true
    };

    struct SpriteGeometry
    {
        juce::Point<float> anchor;
        float capRadius; // 0 = not a rotating-cap sprite
        float minAngleDeg, maxAngleDeg;
    };

    SpriteGeometry geometryForKind (const juce::String& kind)
    {
        if (kind == "selector")
            return { { selectorAnchorX, selectorAnchorY }, selectorCapRadius,
                     -selectorSweepDeg * 0.5f, selectorSweepDeg * 0.5f };

        if (kind == "toggle")
            return { { toggleAnchorX, toggleAnchorY }, 0.0f, 0.0f, 0.0f };

        return { { knobAnchorX, knobAnchorY }, knobCapRadius,
                 -knobSweepDeg * 0.5f, knobSweepDeg * 0.5f };
    }
}

OvertureAudioProcessorEditor::OvertureAudioProcessorEditor (OvertureAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      manifest (basilica::gui::LayoutManifest::parse (BinaryData::layout_manifest_json,
                                                      BinaryData::layout_manifest_jsonSize)),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit)),
      typography (BinaryData::EBGaramondRegular_ttf, BinaryData::EBGaramondRegular_ttfSize,
                  BinaryData::EBGaramondSemiBold_ttf, BinaryData::EBGaramondSemiBold_ttfSize)
{
    // A structurally broken manifest must fail loudly in development and
    // degrade to a plate-only editor in production, never crash.
    jassert (manifest.isValid());

    plateImage = loadImage (BinaryData::plate_overture_png, BinaryData::plate_overture_pngSize);
    knobSprite = loadImage (BinaryData::sprite_knob_brass_png, BinaryData::sprite_knob_brass_pngSize);
    selectorSprite = loadImage (BinaryData::sprite_selector_stepped_png, BinaryData::sprite_selector_stepped_pngSize);
    toggleSprite = loadImage (BinaryData::sprite_toggle_up_png, BinaryData::sprite_toggle_up_pngSize);

    // Creation order doubles as the keyboard focus order (JUCE's default
    // FocusTraverser walks children in z-order = creation order): preset
    // bar + scale control first, then every manifest control in the
    // manifest's own reading order (top band selectors, knob rows in
    // signal-flow order, the bypass lever last).
    addAndMakeVisible (presetBar);

    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    buildControlsFromManifest();

    setResizable (false, false);

    const auto storedStep = (int) audioProcessor.apvts.state.getProperty (uiScaleStepProperty, 0);
    applyScaleStep (juce::jlimit (0, (int) scaleSteps.size() - 1, storedStep));
}

OvertureAudioProcessorEditor::~OvertureAudioProcessorEditor() = default;

juce::Image OvertureAudioProcessorEditor::spriteImageFor (const juce::String& spriteKey) const
{
    if (spriteKey == "selector_stepped")
        return selectorSprite;

    if (spriteKey == "toggle_up")
        return toggleSprite;

    return knobSprite;
}

void OvertureAudioProcessorEditor::buildControlsFromManifest()
{
    for (const auto& entry : manifest.controls)
    {
        auto* parameter = audioProcessor.apvts.getParameter (entry.id);
        jassert (parameter != nullptr); // manifest out of sync with ParameterLayout.cpp
        if (parameter == nullptr)
            continue;

        const auto title = parameter->getName (64);
        const auto geometry = geometryForKind (entry.kind);

        if (entry.kind == "toggle")
        {
            Toggle toggle;
            toggle.entry = &entry;
            toggle.button = std::make_unique<basilica::gui::SpriteToggle> (
                spriteImageFor (entry.sprite), geometry.anchor, title);
            addAndMakeVisible (*toggle.button);
            toggle.attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, entry.id, *toggle.button);
            toggles.push_back (std::move (toggle));
            continue;
        }

        Knob knob;
        knob.entry = &entry;
        knob.slider = std::make_unique<basilica::gui::MasterCropKnob> (
            spriteImageFor (entry.sprite), geometry.anchor, geometry.capRadius, 0.94f,
            geometry.minAngleDeg, geometry.maxAngleDeg);

        knob.slider->setPopupDisplayEnabled (true, true, this);
        knob.slider->setTitle (title);
        knob.slider->setName (title);
        addAndMakeVisible (*knob.slider);

        const auto defaultValue = parameter->getNormalisableRange().convertFrom0to1 (parameter->getDefaultValue());
        knob.slider->setDoubleClickReturnValue (true, defaultValue);

        // SliderAttachment MUST be constructed before the
        // textFromValueFunction override below - JUCE 8.0.14's
        // SliderParameterAttachment constructor itself assigns
        // slider.textFromValueFunction as part of wiring the attachment,
        // which would silently clobber an override set beforehand.
        knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, entry.id, *knob.slider);

        knob.slider->textFromValueFunction = [parameter] (double v)
        {
            auto text = parameter->getText (parameter->convertTo0to1 ((float) v), 0);
            const auto label = parameter->getLabel();
            return label.isNotEmpty() ? text + " " + label : text;
        };
        knob.slider->updateText();

        knobs.push_back (std::move (knob));
    }
}

void OvertureAudioProcessorEditor::cycleScale()
{
    applyScaleStep ((scaleStepIndex + 1) % (int) scaleSteps.size());
}

void OvertureAudioProcessorEditor::applyScaleStep (int newStepIndex)
{
    scaleStepIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, newStepIndex);
    audioProcessor.apvts.state.setProperty (uiScaleStepProperty, scaleStepIndex, nullptr);

    const auto percentText = juce::String ((int) (scaleSteps[(size_t) scaleStepIndex] * 100.0f)) + "%";
    scaleButton.setButtonText (percentText);
    scaleButton.setTitle ("Window scale, " + percentText);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];

    setSize ((int) std::lround ((float) baseEditorWidth * scale),
             (int) std::lround ((float) baseEditorHeight * scale));
}

float OvertureAudioProcessorEditor::plateScale() const noexcept
{
    return plateToUnit * scaleSteps[(size_t) scaleStepIndex];
}

juce::Point<float> OvertureAudioProcessorEditor::plateOrigin() const noexcept
{
    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    return { 0.0f, (float) (topStripHeight1x + topStripGap1x) * scale };
}

void OvertureAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];

    // Top chrome strip behind the preset bar + scale button.
    const auto stripHeight = (float) topStripHeight1x * scale;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17141a), 0.0f, 0.0f,
                                             juce::Colour (0xff0b090d), 0.0f, stripHeight, false));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), stripHeight));
    g.setColour (juce::Colour (0xff5a4420));
    g.fillRect (juce::Rectangle<float> (0.0f, stripHeight - 1.0f * scale, (float) getWidth(), 1.0f * scale));

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // 1. The empty family plate.
    if (plateImage.isValid())
    {
        const auto origin = plateOrigin();
        g.drawImage (plateImage,
                     juce::Rectangle<float> (origin.x, origin.y,
                                             (float) plateWidth1x * scale, (float) plateHeight1x * scale),
                     juce::RectanglePlacement::stretchToFit, false);
    }

    // 2. Static control sprites (knob/selector bodies - the rotating caps
    // are MasterCropKnob children drawn after this method returns;
    // toggles draw their own sprite entirely, see SpriteToggle.h).
    drawStaticSprites (g);

    // 3. Engraved lettering - after the sprites so each label sits on top
    // of its control's feathered basalt patch, still under all children.
    drawPlateLettering (g);
}

void OvertureAudioProcessorEditor::drawStaticSprites (juce::Graphics& g) const
{
    const auto k = plateScale();
    const auto origin = plateOrigin();

    for (const auto& entry : manifest.controls)
    {
        if (entry.kind == "toggle")
            continue; // SpriteToggle children own their full visual

        const auto sprite = spriteImageFor (entry.sprite);
        if (! sprite.isValid())
            continue;

        const auto geometry = geometryForKind (entry.kind);
        const auto drawScale = entry.scale * k;

        const auto transform = juce::AffineTransform::scale (drawScale)
                                   .translated (origin.x + (entry.cx - geometry.anchor.x * entry.scale) * k,
                                                origin.y + (entry.cy - geometry.anchor.y * entry.scale) * k);

        g.drawImageTransformed (sprite, transform);
    }
}

void OvertureAudioProcessorEditor::drawPlateLettering (juce::Graphics& g) const
{
    const auto k = plateScale();
    const auto origin = plateOrigin();
    const auto uiScale = scaleSteps[(size_t) scaleStepIndex];

    for (const auto& entry : manifest.controls)
    {
        if (entry.label.isEmpty() || entry.labelCy <= 0.0f)
            continue;

        const juce::Rectangle<float> box (origin.x + (entry.cx - labelBoxWidthPlatePx * 0.5f) * k,
                                          origin.y + (entry.labelCy - labelBoxHeightPlatePx * 0.5f) * k,
                                          labelBoxWidthPlatePx * k,
                                          labelBoxHeightPlatePx * k);

        typography.drawEngraved (g, entry.label, box, uiScale, plateLabelStyle);
    }
}

void OvertureAudioProcessorEditor::resized()
{
    const auto uiScale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [uiScale] (int v) { return (int) std::lround ((float) v * uiScale); };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)).reduced (0, s (2)));
    presetBar.setBounds (topStrip.reduced (0, s (2)));

    const auto k = plateScale();
    const auto origin = plateOrigin();

    for (const auto& knob : knobs)
    {
        const auto& entry = *knob.entry;
        const auto geometry = geometryForKind (entry.kind);

        // Bounds sized to EXACTLY the crop canvas at the sprite's drawn
        // scale, so the rotating cap registers pixel-true on the static
        // sprite underneath (see MasterCropKnob::cropCanvasSizeFor()).
        const auto side = (float) basilica::gui::MasterCropKnob::cropCanvasSizeFor (geometry.capRadius)
                           * entry.scale * k;

        knob.slider->setBounds (juce::Rectangle<float> (side, side)
                                    .withCentre ({ origin.x + entry.cx * k, origin.y + entry.cy * k })
                                    .getSmallestIntegerContainer());
    }

    for (const auto& toggle : toggles)
    {
        const auto& entry = *toggle.entry;
        const auto geometry = geometryForKind (entry.kind);
        const auto sprite = spriteImageFor (entry.sprite);
        if (! sprite.isValid())
            continue;

        const auto w = (float) sprite.getWidth() * entry.scale * k;
        const auto h = (float) sprite.getHeight() * entry.scale * k;
        const auto x = origin.x + (entry.cx - geometry.anchor.x * entry.scale) * k;
        const auto y = origin.y + (entry.cy - geometry.anchor.y * entry.scale) * k;

        toggle.button->setBounds (juce::Rectangle<float> (x, y, w, h).getSmallestIntegerContainer());
    }
}
