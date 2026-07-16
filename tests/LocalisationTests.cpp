#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <array>

// M2 i18n frame tests (.scaffold/specs/preset-system-m2.md's "I18N" section):
// the German mapping parses, every TRANS() key the M2 PresetBar/PresetManager
// actually use (src/presets/PresetBar.cpp/PresetManager.cpp, copied verbatim
// from the Nave pilot per docs/design-brief.md) is present in
// resources/i18n/de.txt, and Overture's own core/DSP parameter names are
// verifiably NOT present as translation keys (they must never be
// translated - see src/presets/Localisation.h's docs).
namespace
{
    // Mirrors the exact TRANS() call sites in src/presets/PresetBar.cpp and
    // src/presets/PresetManager.cpp (`grep -oh 'TRANS ("[^"]*")'
    // src/presets/*.cpp`) - kept as an explicit list here (rather than
    // parsed from source at test time) so a future edit to those files that
    // silently drops a resources/i18n/de.txt entry is caught by this test
    // failing, not by a runtime English-fallback nobody notices.
    constexpr std::array<const char*, 17> expectedFrameKeys {
        "Cancel",
        "Delete",
        "Enter a name for the new preset:",
        "Export preset...",
        "Export...",
        "Factory",
        "Import a preset or preset bank...",
        "Import failed",
        "Import...",
        "Init",
        "Preset name",
        "Save As...",
        "Save",
        "Set current as default",
        "This file is not a valid preset.",
        "This preset file belongs to a different plugin.",
        "This preset was saved by an incompatible version of the preset format.",
    };

    // Core/DSP parameter names (ParameterLayout.cpp's user-facing display
    // strings) - must NEVER appear as translation keys, per the binding
    // spec's "NEVER translate core/DSP terminology" rule.
    constexpr std::array<const char*, 11> parameterDisplayNames {
        "Tight", "Drive", "Bite", "Knee Soften", "Asymmetry", "Bite Tilt",
        "Level", "Mix", "Bypass", "Voicing", "Oversampling",
    };

    juce::LocalisedStrings loadGermanMappings()
    {
        const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
        return juce::LocalisedStrings (text, true);
    }
}

TEST_CASE ("i18n: resources/i18n/de.txt parses without error and reports German as its language", "[i18n]")
{
    const auto mappings = loadGermanMappings();
    CHECK (mappings.getLanguageName() == juce::String ("German"));
}

TEST_CASE ("i18n: every TRANS() key used by the M2 preset frame is present in de.txt", "[i18n]")
{
    auto mappings = loadGermanMappings();
    constexpr auto sentinel = "__OVERTURE_MISSING_TRANSLATION__";

    for (const auto* key : expectedFrameKeys)
    {
        const auto translated = mappings.translate (juce::String (key), juce::String (sentinel));
        CHECK (translated != juce::String (sentinel));
        CHECK (translated.isNotEmpty());
    }
}

TEST_CASE ("i18n: core/DSP parameter names are verifiably NOT present as translation keys", "[i18n]")
{
    auto mappings = loadGermanMappings();
    constexpr auto sentinel = "__OVERTURE_MISSING_TRANSLATION__";

    for (const auto* name : parameterDisplayNames)
    {
        const auto translated = mappings.translate (juce::String (name), juce::String (sentinel));
        CHECK (translated == juce::String (sentinel)); // i.e. NOT found as a key - falls through unchanged
    }
}
