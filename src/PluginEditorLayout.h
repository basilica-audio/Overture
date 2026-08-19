#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Overture's own @1x faceplate/control-bay geometry table - lives in its own
// header, rather than as an anonymous-namespace block inside
// PluginEditor.cpp, so tests/gui/EditorLayoutTests.cpp can assert layout
// invariants directly against the SAME numbers PluginEditor.cpp actually lays
// components out with, instead of a second hand-copied set of constants that
// could silently drift out of sync (pattern copied from
// basilica-audio/silentium's M3 pilot, see that repo's PluginEditorLayout.h).
//
// This is Overture-specific, art-authored geometry (derived 1:1 from
// .scaffold/gui-assets/faceplate-overture-v1/layout-manifest.json, which is
// itself the ground truth ../render_faceplate.py's Blender scene was
// generated from - unlike Silentium's hand-built pilot plate, Overture's
// faceplate went through the suite's generalized faceplate generator, so its
// bay rectangles below are copied verbatim from the manifest's px @1x
// coordinates (converted from the manifest's centre+size form to a top-left
// juce::Rectangle)).
namespace ovtr::layout
{
    // juce::Rectangle/Point's constructors are not constexpr (JUCE 8.0.14),
    // so the rects below are plain namespace-scope consts rather than true
    // constexpr - still zero-initialisation-order risk since they only
    // depend on integer literals.
    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 600;

    // Header band + roundel: auto-derived proportions from
    // render_faceplate.py's build_faceplate() (header_w = plate_w*0.833,
    // header_h = plate_h*0.043, header_cy = plate_h*0.367 measured from the
    // plate's vertical centre; roundel major_radius = plate_h*0.065),
    // converted from Blender's centred/Y-up unit space back to this table's
    // top-left/Y-down px convention - see that script's px_rect_to_blender()
    // for the forward conversion this inverts. The header bay's own height
    // here (90) is generously taller than the engraved groove's own render
    // height (~26px) to leave room for the JUCE-drawn title label's font,
    // matching Silentium's headerBay1x precedent (also taller than its own
    // engraved groove for the same reason).
    const juce::Rectangle<int> headerBay1x { 75, 35, 750, 90 };
    const juce::Point<int> roundelCentre1x { 450, 80 };
    constexpr int roundelRadius1x = 39;

    // The four bays below are copied verbatim (converted from centre+size to
    // top-left) from faceplate-overture-v1/layout-manifest.json - see that
    // file and its sibling README.md for the exact param-ID-to-bay mapping
    // this table's KnobLayoutEntry/ChoiceLayoutEntry/ToggleLayoutEntry arrays
    // (PluginEditor.cpp) must stay faithful to:
    //   input   { cx:225, cy:270, w:340, h:150 } -> tight, drive
    //   clipper { cx:675, cy:270, w:340, h:150 } -> voicing, asymmetryAmount,
    //                                                kneeSoften, biteAmount
    //   output  { cx:225, cy:450, w:340, h:150 } -> biteTilt, level, mix
    //   utility { cx:675, cy:450, w:340, h:150 } -> bypass, oversampling
    const juce::Rectangle<int> inputBay1x { 55, 195, 340, 150 };
    const juce::Rectangle<int> clipperBay1x { 505, 195, 340, 150 };
    const juce::Rectangle<int> outputBay1x { 55, 375, 340, 150 };
    const juce::Rectangle<int> utilityBay1x { 505, 375, 340, 150 };

    // Extra strip above the plate art for the preset bar + scale control -
    // interactive text/menus don't fit the plate's own thin engraved header
    // groove at any legible size, so they live in their own band instead,
    // same as Silentium's precedent.
    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };

    // Per-bay single-row control grid: each bay lays its own N controls out
    // in one row of N equal-width cells (unlike Silentium's single 5x2 knob
    // grid, Overture's manifest groups controls into four independent,
    // differently-sized bays - see faceplate-overture-v1/README.md's "Bay
    // design" section). A common control size is used across every bay
    // (knobs, the bypass toggle, and the two combo boxes) so the faceplate
    // reads consistently regardless of which bay a control sits in; the
    // widest row (clipper, 4 controls) is the binding constraint on cell
    // width (340 / 4 = 85px), which knobDiameter1x below is sized to clear.
    constexpr int knobLabelHeight1x = 16;
    constexpr int knobDiameter1x = 72;
    constexpr int toggleSize1x = 56;
    constexpr int comboBoxHeight1x = 26;
    constexpr int maxControlsPerBayRow = 4;
}
