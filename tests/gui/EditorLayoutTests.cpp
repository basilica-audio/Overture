#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"

#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

// Wave-3 compositional-layout invariants, asserted against the SAME parsed
// manifest the editor composites from (PluginEditor::layoutManifest() /
// gui/LayoutManifest.h) - never a second hand-maintained coordinate list.
// The expected control census comes from the rollout control inventory
// (.scaffold/gui-assets/rollout-2026-07/overture/control-inventory.md):
// 8 knobs (5+3 staggered rows), 2 stepped selectors, 1 toggle, 0 meters.
namespace
{
    basilica::gui::LayoutManifest parseManifest()
    {
        return basilica::gui::LayoutManifest::parse (BinaryData::layout_manifest_json,
                                                     BinaryData::layout_manifest_jsonSize);
    }

    // The plate's usable control field (inside the gold pinstripe border),
    // measured on plate_overture.png - see the wave-3 rollout measurement
    // log. Slightly conservative on purpose.
    constexpr float fieldLeft = 85.0f, fieldRight = 1162.0f;
    constexpr float fieldTop = 85.0f, fieldBottom = 768.0f;

    // Baked central divider flourish (measured: y 448..459, x 510..740) -
    // no control cap may cover it.
    const juce::Rectangle<float> dividerKeepOut (500.0f, 444.0f, 250.0f, 20.0f);

    float capRadiusPlatePx (const basilica::gui::ManifestControl& control)
    {
        using namespace ovtr::layout;

        if (control.kind == "selector")
            return selectorCapRadius * control.scale;

        if (control.kind == "toggle")
            return 48.0f * control.scale; // housing half-height, conservative

        return knobCapRadius * control.scale;
    }
}

TEST_CASE ("Manifest parses and matches the rollout control inventory census", "[gui][layout]")
{
    const auto manifest = parseManifest();

    REQUIRE (manifest.isValid());
    CHECK (manifest.plateWidthPx == ovtr::layout::plateCanvasWidthPx);
    CHECK (manifest.plateHeightPx == ovtr::layout::plateCanvasHeightPx);

    CHECK (manifest.ofKind ("knob").size() == 8);
    CHECK (manifest.ofKind ("selector").size() == 2);
    CHECK (manifest.ofKind ("toggle").size() == 1);
    CHECK (manifest.ofKind ("meter").empty()); // 0 meters is a DSP-justified inventory decision
    CHECK (manifest.controls.size() == 11);
}

TEST_CASE ("Every manifest control id resolves to a real APVTS parameter of the right type", "[gui][layout]")
{
    const auto manifest = parseManifest();
    REQUIRE (manifest.isValid());

    OvertureAudioProcessor processor;

    for (const auto& control : manifest.controls)
    {
        auto* parameter = processor.apvts.getParameter (control.id);
        INFO ("manifest id \"" << control.id.toStdString() << "\"");
        REQUIRE (parameter != nullptr);

        if (control.kind == "toggle")
            CHECK (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr);
        else if (control.kind == "selector")
            CHECK (dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr);
        else if (control.kind == "knob")
            CHECK (dynamic_cast<juce::AudioParameterFloat*> (parameter) != nullptr);
    }
}

TEST_CASE ("Knob rows follow the 5+3 staggered family signature", "[gui][layout]")
{
    const auto manifest = parseManifest();
    REQUIRE (manifest.isValid());

    std::map<float, std::vector<float>> rows; // cy -> sorted cx list

    for (const auto* knob : manifest.ofKind ("knob"))
        rows[knob->cy].push_back (knob->cx);

    REQUIRE (rows.size() == 2);

    auto it = rows.begin();
    auto& row1 = it->second;
    auto& row2 = std::next (it)->second;

    CHECK (row1.size() == 5);
    CHECK (row2.size() == 3);

    for (auto* row : { &row1, &row2 })
    {
        std::sort (row->begin(), row->end());

        // Uniform spacing within a row (the LAYOUT-INVARIANTE: same-role
        // elements share a common axis and even rhythm).
        for (size_t i = 2; i < row->size(); ++i)
            CHECK (std::abs (((*row)[i] - (*row)[i - 1]) - ((*row)[1] - (*row)[0])) < 1.0f);
    }

    // Staggered: the second row's grid must not simply reuse the first
    // row's x positions.
    std::set<float> row1Xs (row1.begin(), row1.end());
    int shared = 0;
    for (const auto x : row2)
        shared += row1Xs.count (x) > 0 ? 1 : 0;

    CHECK (shared < (int) row2.size());
}

TEST_CASE ("Selectors share a row and toggles sit clear of the knob field", "[gui][layout]")
{
    const auto manifest = parseManifest();
    REQUIRE (manifest.isValid());

    const auto selectors = manifest.ofKind ("selector");
    REQUIRE (selectors.size() == 2);
    CHECK (selectors[0]->cy == selectors[1]->cy);
    CHECK (selectors[0]->cx != selectors[1]->cx);
}

TEST_CASE ("Every control stays inside the pinstripe field and off the divider flourish", "[gui][layout]")
{
    const auto manifest = parseManifest();
    REQUIRE (manifest.isValid());

    for (const auto& control : manifest.controls)
    {
        const auto r = capRadiusPlatePx (control);
        INFO ("control \"" << control.id.toStdString() << "\"");

        CHECK (control.cx - r >= fieldLeft);
        CHECK (control.cx + r <= fieldRight);
        CHECK (control.cy - r >= fieldTop);
        CHECK (control.cy + r <= fieldBottom);

        const juce::Rectangle<float> capBox (control.cx - r, control.cy - r, 2.0f * r, 2.0f * r);
        CHECK_FALSE (capBox.intersects (dividerKeepOut));

        if (control.labelCy > 0.0f)
        {
            using namespace ovtr::layout;
            const juce::Rectangle<float> labelBox (control.cx - labelBoxWidthPlatePx * 0.5f,
                                                   control.labelCy - labelBoxHeightPlatePx * 0.5f,
                                                   labelBoxWidthPlatePx, labelBoxHeightPlatePx);

            CHECK (labelBox.getY() >= fieldTop);
            CHECK (labelBox.getBottom() <= fieldBottom);

            // Lettering never intrudes into its own control's rotating cap.
            CHECK (labelBox.getY() >= control.cy + r - 1.0f);
        }
    }
}

TEST_CASE ("No two interactive caps overlap", "[gui][layout]")
{
    const auto manifest = parseManifest();
    REQUIRE (manifest.isValid());

    for (size_t a = 0; a < manifest.controls.size(); ++a)
    {
        for (size_t b = a + 1; b < manifest.controls.size(); ++b)
        {
            const auto& ca = manifest.controls[a];
            const auto& cb = manifest.controls[b];

            const auto minGap = capRadiusPlatePx (ca) + capRadiusPlatePx (cb);
            const auto dx = ca.cx - cb.cx;
            const auto dy = ca.cy - cb.cy;

            INFO (ca.id.toStdString() << " vs " << cb.id.toStdString());
            CHECK (dx * dx + dy * dy >= minGap * minGap);
        }
    }
}

TEST_CASE ("Editor base size derives from the plate geometry", "[gui][layout]")
{
    using namespace ovtr::layout;

    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    OvertureAudioProcessorEditor editor (processor);

    // A fresh processor carries no stored uiScaleStep, so the editor
    // constructs at the 100% step and its size IS the base geometry.
    CHECK (editor.getWidth() == baseEditorWidth);
    CHECK (editor.getHeight() == baseEditorHeight);
    CHECK (editor.layoutManifest().isValid());
}
