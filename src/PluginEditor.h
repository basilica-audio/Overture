#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "gui/LayoutManifest.h"
#include "gui/MasterCropKnob.h"
#include "gui/PlateTypography.h"
#include "gui/SpriteToggle.h"
#include "presets/PresetBar.h"

class OvertureAudioProcessor;

// Wave-3 COMPOSITIONAL photoreal editor (campaign 2026-08, supersedes the
// filmstrip-generation M3 editor): the accepted EMPTY family plate render
// (resources/gui/plate_overture.png) is the sole baked background, and
// every control is composited live from the extracted control-sprite
// library at the coordinates in resources/gui/layout_manifest.json (the
// single source of truth - see gui/LayoutManifest.h). Draw order:
//
//   1. plate render (paint())
//   2. static control sprites - knob/selector bodies at their manifest
//      positions (paint(), under the children)
//   3. engraved lettering - PlateTypography, gilded gold on the dark
//      basalt (paint(), after the sprites so labels sit on top of each
//      sprite's feathered basalt patch)
//   4. rotating cap crops - one MasterCropKnob child per knob/selector,
//      rotating a feathered circular crop of its own sprite's cap (the
//      suite INNER-DISC technique: rim + housing stay static underneath)
//   5. lever toggles - SpriteToggle children drawing their own full
//      sprite (up = ON, mirrored = OFF; see SpriteToggle.h's asset-gap
//      docs)
//
// Overture-specific control set (rollout-2026-07/overture/
// control-inventory.md): 8 continuous knobs in the 5+3 staggered family
// rows, 2 stepped selectors (voicing/oversampling) in the top band, the
// single bypass lever, and deliberately ZERO meters (no metering DSP
// exists in OvertureEngine - rendering dials with nothing behind them
// would violate the suite's no-dead-decoration rule).
//
// Window scaling is STEPPED (100/150/200%, UA-style corner control,
// persisted as a plain property on the APVTS state tree), matching every
// merged M3 editor in the suite.
class OvertureAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OvertureAudioProcessorEditor (OvertureAudioProcessor& processorToEdit);
    ~OvertureAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // The parsed layout manifest - exposed read-only so tests assert
    // layout invariants against the exact data this editor composites
    // from (tests/gui/EditorLayoutTests.cpp).
    const basilica::gui::LayoutManifest& layoutManifest() const noexcept { return manifest; }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        const basilica::gui::ManifestControl* entry = nullptr;
        std::unique_ptr<basilica::gui::MasterCropKnob> slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        const basilica::gui::ManifestControl* entry = nullptr;
        std::unique_ptr<basilica::gui::SpriteToggle> button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    juce::Image spriteImageFor (const juce::String& spriteKey) const;
    void buildControlsFromManifest();
    void applyScaleStep (int newStepIndex);
    void cycleScale();
    void drawStaticSprites (juce::Graphics& g) const;
    void drawPlateLettering (juce::Graphics& g) const;

    // plate-render px -> screen px for the current scale step, and the
    // plate's top-left corner in screen px.
    float plateScale() const noexcept;
    juce::Point<float> plateOrigin() const noexcept;

    OvertureAudioProcessor& audioProcessor;

    basilica::gui::LayoutManifest manifest;

    juce::Image plateImage;
    juce::Image knobSprite, selectorSprite, toggleSprite;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    std::vector<Knob> knobs;
    std::vector<Toggle> toggles;

    basilica::gui::PlateTypography typography;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureAudioProcessorEditor)
};
