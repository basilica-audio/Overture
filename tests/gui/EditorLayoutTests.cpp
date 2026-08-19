#include "PluginEditorLayout.h"

#include <catch2/catch_test_macros.hpp>

// Layout-invariant tests asserted directly against the same ovtr::layout
// constants PluginEditor.cpp lays components out with (see
// PluginEditorLayout.h), so this test and the actual layout can never
// silently drift apart - pattern copied from basilica-audio/silentium's
// EditorLayoutTests.cpp, adapted for Overture's four-bay (input/clipper/
// output/utility) grid rather than Silentium's header/meter/control/aux
// layout.
TEST_CASE ("Header bay starts at or below the plate's top edge and above every control bay", "[gui][layout]")
{
    using namespace ovtr::layout;

    CHECK (headerBay1x.getY() >= 0);
    CHECK (headerBay1x.getBottom() <= inputBay1x.getY());
    CHECK (headerBay1x.getBottom() <= clipperBay1x.getY());
}

TEST_CASE ("The four control bays form a non-overlapping 2x2 grid matching the manifest", "[gui][layout]")
{
    using namespace ovtr::layout;

    // input/output share a left column, clipper/utility share a right
    // column - see faceplate-overture-v1/layout-manifest.json's cx values
    // (225 vs 675 on a 900-wide canvas).
    CHECK (inputBay1x.getX() == outputBay1x.getX());
    CHECK (clipperBay1x.getX() == utilityBay1x.getX());
    CHECK (inputBay1x.getRight() <= clipperBay1x.getX());

    // input/clipper share a top row, output/utility share a bottom row -
    // see the manifest's cy values (270 vs 450).
    CHECK (inputBay1x.getY() == clipperBay1x.getY());
    CHECK (outputBay1x.getY() == utilityBay1x.getY());
    CHECK (inputBay1x.getBottom() <= outputBay1x.getY());
}

TEST_CASE ("Every bay rect matches faceplate-overture-v1/layout-manifest.json exactly", "[gui][layout]")
{
    using namespace ovtr::layout;

    // Manifest centre+size rects, converted to top-left form (cx - w/2,
    // cy - h/2, w, h) - see PluginEditorLayout.h's docs for the conversion.
    CHECK (inputBay1x == juce::Rectangle<int> (55, 195, 340, 150));
    CHECK (clipperBay1x == juce::Rectangle<int> (505, 195, 340, 150));
    CHECK (outputBay1x == juce::Rectangle<int> (55, 375, 340, 150));
    CHECK (utilityBay1x == juce::Rectangle<int> (505, 375, 340, 150));
}

TEST_CASE ("Every bay cell is tall enough for a label plus a full-diameter knob with no overlap", "[gui][layout]")
{
    using namespace ovtr::layout;

    for (const auto& bay : { inputBay1x, clipperBay1x, outputBay1x, utilityBay1x })
        CHECK (bay.getHeight() - knobLabelHeight1x >= knobDiameter1x);
}

TEST_CASE ("The widest bay row (clipper, 4 controls) still fits the shared knob diameter per cell", "[gui][layout]")
{
    using namespace ovtr::layout;

    const auto cellW = clipperBay1x.getWidth() / maxControlsPerBayRow;
    CHECK (cellW >= knobDiameter1x);
}

TEST_CASE ("Every laid-out bay stays within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace ovtr::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    for (const auto& bay : { headerBay1x, inputBay1x, clipperBay1x, outputBay1x, utilityBay1x })
        CHECK (plateCanvas.contains (bay));
}
