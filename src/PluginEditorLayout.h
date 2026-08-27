#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

// Overture's wave-3 COMPOSITIONAL faceplate geometry (campaign 2026-08,
// .scaffold/gui-assets/rollout-2026-07): the plate is the accepted EMPTY
// family plate render (resources/gui/plate_overture.png, wave-2 job
// 5fc01211 - obsidian panel, gold chamfer + pinstripe, corner filigree,
// 4 corner screws, central divider flourish, 2 amber vent grilles, NO
// baked controls), and every control is composited live from the extracted
// control-sprite library (.scaffold/gui-assets/sprite-library/, see its
// provenance.md) at the coordinates in resources/gui/layout_manifest.json.
//
// This header carries only what is NOT per-control position data (that
// lives in the manifest, the single source of truth - see
// gui/LayoutManifest.h): editor chrome (top strip, stepped scale),
// plate-to-@1x scaling, and the per-SPRITE-FAMILY intrinsic geometry
// (anchor points, cap radii - measured once against the sprite PNGs
// themselves, see each entry's provenance note).
//
// SUPERSEDES the M3 filmstrip-generation table this file previously held
// (bay rects for the faceplate-overture-v1 art). The filmstrip components
// (src/gui/FilmstripKnob.h, FilmstripToggle.h) stay in the tree per the
// suite's "superseded, not deleted" convention, but this editor no longer
// uses them.
namespace ovtr::layout
{
    // Plate render canvas (1k family plate) and its @1x on-screen size -
    // same 900/1264 ratio as silentium's master-05 baseline (slnt::layout).
    constexpr int plateCanvasWidthPx = 1264;
    constexpr int plateCanvasHeightPx = 848;
    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 604;

    // plate-render px -> @1x px.
    constexpr float plateToUnit = (float) plateWidth1x / (float) plateCanvasWidthPx;

    // Top chrome strip (preset bar + stepped scale button), same layout
    // family as the merged M3 editors.
    constexpr int topStripHeight1x = 36;
    constexpr int topStripGap1x = 4;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };

    // ==================== sprite-family intrinsic geometry ====================
    // Anchor = the point in the sprite's own pixel space that the layout
    // manifest's cx/cy positions. Measured against the sprite PNGs
    // (sprite-library extraction wave 2026-08-27, radial-profile /
    // blob-centroid analysis - see the wave-3 rollout brief's measurement
    // log). Cap radius = the rotating brass cap's radius in sprite px,
    // from the radial luminance profile's plateau edge.

    // sprite_knob_brass.png (148x148, from master-05 row-1 far-right knob):
    // cap centre (75.5, 70.0), cap plateau to r~30, rolloff complete ~r39.
    constexpr float knobAnchorX = 75.5f;
    constexpr float knobAnchorY = 70.0f;
    constexpr float knobCapRadius = 34.0f;
    constexpr float knobSpriteSizePx = 148.0f;

    // sprite_selector_stepped.png (188x210, overture wave-1 redo crop -
    // stepped selector: brass cap + engraved tick crown, crown static):
    // cap centre (85, 117), cap plateau to r~24, rim highlight to ~28.
    constexpr float selectorAnchorX = 85.0f;
    constexpr float selectorAnchorY = 117.0f;
    constexpr float selectorCapRadius = 28.0f;

    // Selector rotation span: a stepped hardware switch's short throw,
    // NOT the 270-degree continuous-knob sweep. Centre detent = the
    // sprite's own baked pose (zero live rotation at proportion 0.5,
    // MasterCropKnob convention).
    constexpr float selectorSweepDeg = 90.0f;

    // sprite_toggle_up.png (117x129, overture wave-1 redo crop - family
    // lever toggle, lever up = ON): housing centre (58, 68).
    constexpr float toggleAnchorX = 58.0f;
    constexpr float toggleAnchorY = 68.0f;

    // Continuous-knob sweep (suite standard, matches the Blender filmstrip
    // and MasterCropKnob defaults).
    constexpr float knobSweepDeg = 270.0f;

    // ==================== engraved lettering ====================
    // Label box @plate-px, centred on each control's manifest cx at the
    // manifest's labelCy.
    constexpr float labelBoxWidthPlatePx = 150.0f;
    constexpr float labelBoxHeightPlatePx = 26.0f;
}
