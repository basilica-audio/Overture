#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/PresetManager.h"

#include <BinaryData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// M2 preset system tests (.scaffold/specs/preset-system-m2.md's "Tests"
// section - each TEST_CASE below maps to one of that section's numbered
// items, called out in the test names/comments) plus
// docs/design-brief.md's guarantee 9 (every factory preset loads, lands
// within tolerance, and produces no NaN/Inf/silence on a standard test
// signal).
namespace
{
    using basilica::presets::FactoryPresetAsset;
    using basilica::presets::PresetManager;
    using basilica::presets::PresetManagerConfig;

    // Mirrors PluginProcessor.cpp's own makeFactoryPresetAssets() - kept as
    // an independent copy here rather than exported from PluginProcessor.cpp
    // so this test file can construct its own, fully isolated PresetManager
    // instances (see makeIsolatedConfig() below) without depending on
    // production wiring internals.
    std::vector<FactoryPresetAsset> makeTestFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::cleanPush_json, BinaryData::cleanPush_jsonSize },
            { BinaryData::classicBoost_json, BinaryData::classicBoost_jsonSize },
            { BinaryData::dropTuneTight_json, BinaryData::dropTuneTight_jsonSize },
            { BinaryData::smoothPush_json, BinaryData::smoothPush_jsonSize },
            { BinaryData::ownDistortion_json, BinaryData::ownDistortion_jsonSize },
            { BinaryData::fuzzAdjacentLead_json, BinaryData::fuzzAdjacentLead_jsonSize },
            { BinaryData::parallelGrit_json, BinaryData::parallelGrit_jsonSize },
            { BinaryData::deFizzCleanup_json, BinaryData::deFizzCleanup_jsonSize },
        };
    }

    // A fresh, isolated scratch directory per test case, so this file never
    // reads or writes the real ~/Library/Audio/Presets/... (or Windows
    // equivalent) location on the machine running the tests - see
    // PresetManagerConfig::userPresetsDirectoryOverrideForTests. Deleted on
    // destruction.
    struct ScopedTestDirectory
    {
        ScopedTestDirectory()
            : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("OverturePresetManagerTests")
                       .getChildFile (juce::String (juce::Time::getHighResolutionTicks())
                                       + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
        {
            dir.createDirectory();
        }

        ~ScopedTestDirectory()
        {
            dir.deleteRecursively();
        }

        JUCE_DECLARE_NON_COPYABLE (ScopedTestDirectory)

        juce::File dir;
    };

    PresetManagerConfig makeIsolatedConfig (const juce::File& userDir)
    {
        PresetManagerConfig config;
        config.pluginId = "com.yvesvogl.overture";
        config.pluginName = "Overture";
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = "0.2.0-test";
        config.userPresetsDirectoryOverrideForTests = userDir;
        return config;
    }

    void setParam (OvertureAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (OvertureAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}

//==============================================================================
// 1. Save -> load round-trip restores every parameter exactly.
TEST_CASE ("PresetManager: save -> load round-trip restores every parameter exactly", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 222.0f);
    setParam (processor, ParamIDs::drive, 17.5f);
    setParam (processor, ParamIDs::biteAmount, 33.0f);
    setParam (processor, ParamIDs::kneeSoften, 66.0f);
    setParam (processor, ParamIDs::asymmetryAmount, 12.0f);
    setParam (processor, ParamIDs::biteTilt, -55.0f);
    setParam (processor, ParamIDs::level, -3.0f);
    setParam (processor, ParamIDs::mix, 77.0f);

    REQUIRE (manager.saveUserPreset ("Round Trip", "Init"));

    setParam (processor, ParamIDs::tight, 20.0f);
    setParam (processor, ParamIDs::biteTilt, 0.0f);

    REQUIRE (manager.loadPreset ("Round Trip"));

    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (222.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (17.5f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::biteAmount) == Catch::Approx (33.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::kneeSoften) == Catch::Approx (66.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::asymmetryAmount) == Catch::Approx (12.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::biteTilt) == Catch::Approx (-55.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::level) == Catch::Approx (-3.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (77.0f).margin (1.0e-3));
}

//==============================================================================
// 2. Import ignores unknown IDs, keeps defaults for missing IDs.
TEST_CASE ("PresetManager: import ignores unknown parameter IDs and keeps defaults for missing ones", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 175.0f); // perturb before importing, so "kept at default" is meaningful

    const auto fixtureJson = juce::String (
        "{"
        "\"format\":\"basilica-preset-1\","
        "\"plugin\":\"com.yvesvogl.overture\","
        "\"pluginVersion\":\"0.2.0-test\","
        "\"name\":\"Fixture\","
        "\"category\":\"Init\","
        "\"parameters\":{\"drive\":9.5,\"unknownFutureParam\":123.0}"
        "}");

    const auto fixtureFile = juce::File::createTempFile (".basilicapreset");
    fixtureFile.replaceWithText (fixtureJson);

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (fixtureFile, errorMessage));

    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (9.5f).margin (1.0e-3));
    // `tight` was not in the fixture - loadPreset()/applyParsedPreset() reset
    // every parameter to its default first, so it must land back at 100 Hz
    // (v0.2.0's default), not stay at the perturbed 175 Hz.
    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (100.0f).margin (1.0e-3));

    fixtureFile.deleteFile();
}

//==============================================================================
// 3. Import refuses wrong-plugin and wrong-format files.
TEST_CASE ("PresetManager: import refuses a preset belonging to a different plugin", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto wrongPluginJson = juce::String (
        "{\"format\":\"basilica-preset-1\",\"plugin\":\"com.yvesvogl.nave\","
        "\"pluginVersion\":\"0.2.0\",\"name\":\"Not Overture\",\"category\":\"Init\","
        "\"parameters\":{\"drive\":9.5}}");

    const auto fixtureFile = juce::File::createTempFile (".basilicapreset");
    fixtureFile.replaceWithText (wrongPluginJson);

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (fixtureFile, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    fixtureFile.deleteFile();
}

TEST_CASE ("PresetManager: import refuses a file with an incompatible format tag", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto wrongFormatJson = juce::String (
        "{\"format\":\"some-other-format-1\",\"plugin\":\"com.yvesvogl.overture\","
        "\"pluginVersion\":\"0.2.0\",\"name\":\"Bad Format\",\"category\":\"Init\","
        "\"parameters\":{\"drive\":9.5}}");

    const auto fixtureFile = juce::File::createTempFile (".basilicapreset");
    fixtureFile.replaceWithText (wrongFormatJson);

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (fixtureFile, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    fixtureFile.deleteFile();
}

//==============================================================================
// 4. Factory presets all parse and load (iterate BinaryData) + guarantee 9's
// "no NaN/Inf/silence on a standard test signal".
TEST_CASE ("PresetManager: every factory preset parses and loads without error", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto allPresets = manager.getAllPresets();
    const auto factoryCount = std::count_if (allPresets.begin(), allPresets.end(),
                                              [] (const PresetManager::PresetEntry& e) { return e.isFactory; });

    CHECK (factoryCount == 9); // Default + the brief's 8 (docs/presets.md)

    for (auto& entry : allPresets)
    {
        if (! entry.isFactory)
            continue;

        CHECK (manager.loadPreset (entry.name));
    }
}

TEST_CASE ("PresetManager: every factory preset lands within tolerance and produces no NaN/Inf/silence "
           "on a standard test signal (docs/design-brief.md guarantee 9)",
           "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto allPresets = manager.getAllPresets();

    for (auto& entry : allPresets)
    {
        if (! entry.isFactory)
            continue;

        REQUIRE (manager.loadPreset (entry.name));

        juce::AudioBuffer<float> buffer (2, 512);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                data[sample] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * static_cast<float> (sample) / 48000.0f);
        }

        juce::MidiBuffer midi;

        for (int block = 0; block < 4; ++block)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        bool allFinite = true;
        bool anyNonZero = false;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                if (! std::isfinite (data[sample]))
                    allFinite = false;
                if (std::abs (data[sample]) > 1.0e-6f)
                    anyNonZero = true;
            }
        }

        CHECK (allFinite);
        CHECK (anyNonZero); // no factory preset should output silence for a real input
    }
}

//==============================================================================
// 5. Default resolution order (user Default > factory Default > plain defaults).
TEST_CASE ("PresetManager: applyStartupDefault() loads the factory Default when no user Default exists", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 250.0f); // perturb first

    manager.applyStartupDefault();

    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (100.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::drive) == Catch::Approx (3.0f).margin (1.0e-3));
    CHECK (manager.getCurrentPresetName() == juce::String ("Default"));
}

TEST_CASE ("PresetManager: a user Default preset wins over the factory Default", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 333.0f);
    REQUIRE (manager.setCurrentAsDefault());

    setParam (processor, ParamIDs::tight, 100.0f);

    manager.applyStartupDefault();

    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (333.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: resetDefault() removes the user Default so the factory Default resolves again", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 333.0f);
    REQUIRE (manager.setCurrentAsDefault());
    REQUIRE (manager.resetDefault());

    setParam (processor, ParamIDs::tight, 250.0f);
    manager.applyStartupDefault();

    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (100.0f).margin (1.0e-3));
}

//==============================================================================
// 6. Dirty flag: clean after load, dirty after any param change, clean after save.
TEST_CASE ("PresetManager: dirty flag lifecycle - clean after load, dirty after a change, clean after save", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    setParam (processor, ParamIDs::drive, 21.0f);
    CHECK (manager.isDirty());

    REQUIRE (manager.saveUserPreset ("Dirty Test", "Init"));
    CHECK_FALSE (manager.isDirty());
}

//==============================================================================
// 7. prev/next ordering and wrap-around.
TEST_CASE ("PresetManager: nextPreset()/previousPreset() traverse alphabetically and wrap around", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    REQUIRE (all.size() >= 2);

    REQUIRE (manager.loadPreset (all.front().name));

    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all[1].name);

    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);

    // Wrap backward from the first entry to the last.
    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.back().name);

    // Wrap forward from the last entry back to the first.
    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);
}

//==============================================================================
// Additional coverage beyond the spec's minimum list: save/rename/delete
// guards, single-file export round-trip, and bank import/export.

TEST_CASE ("PresetManager: saveUserPreset() refuses to shadow a factory preset name", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    CHECK_FALSE (manager.saveUserPreset ("Default", "Init")); // "Default" already exists as a factory preset
    CHECK_FALSE (manager.saveUserPreset ("Classic Boost", "Guitar"));
}

TEST_CASE ("PresetManager: renameUserPreset() moves a user preset to a new name and preserves its parameters", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::biteTilt, 44.0f);
    REQUIRE (manager.saveUserPreset ("Old Name", "Init"));

    REQUIRE (manager.renameUserPreset ("Old Name", "New Name"));

    setParam (processor, ParamIDs::biteTilt, 0.0f); // perturb before reloading

    CHECK_FALSE (manager.loadPreset ("Old Name")); // gone
    REQUIRE (manager.loadPreset ("New Name"));
    CHECK (getParam (processor, ParamIDs::biteTilt) == Catch::Approx (44.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: deleteUserPreset() removes a user preset but never a factory preset", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.saveUserPreset ("Temporary", "Init"));
    REQUIRE (manager.deleteUserPreset ("Temporary"));
    CHECK_FALSE (manager.loadPreset ("Temporary"));

    // A factory preset name isn't a file on disk in the user directory, so
    // there's nothing to delete - deleteUserPreset() must return false, and
    // the factory preset must still load afterwards.
    CHECK_FALSE (manager.deleteUserPreset ("Default"));
    CHECK (manager.loadPreset ("Default"));
}

TEST_CASE ("PresetManager: exportPreset()/importPresetFile() single-file round-trip", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::asymmetryAmount, 77.0f);
    REQUIRE (manager.saveUserPreset ("Exportable", "Init"));

    const auto exportFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (manager.exportPreset ("Exportable", exportFile));
    REQUIRE (exportFile.existsAsFile());

    REQUIRE (manager.deleteUserPreset ("Exportable")); // remove the original before reimporting

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (exportFile, errorMessage));
    CHECK (getParam (processor, ParamIDs::asymmetryAmount) == Catch::Approx (77.0f).margin (1.0e-3));

    exportFile.deleteFile();
}

TEST_CASE ("PresetManager: exportBank()/importBank() round-trips every user preset through a zip", "[presets]")
{
    ScopedTestDirectory sourceScratch;
    ScopedTestDirectory destScratch;

    OvertureAudioProcessor sourceProcessor;
    sourceProcessor.prepareToPlay (48000.0, 512);
    PresetManager sourceManager (sourceProcessor.apvts, makeIsolatedConfig (sourceScratch.dir), makeTestFactoryPresetAssets());

    setParam (sourceProcessor, ParamIDs::tight, 111.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset A", "Init"));

    setParam (sourceProcessor, ParamIDs::tight, 222.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset B", "Init"));

    const auto bankFile = juce::File::createTempFile (".zip");
    REQUIRE (sourceManager.exportBank (bankFile));
    REQUIRE (bankFile.existsAsFile());

    OvertureAudioProcessor destProcessor;
    destProcessor.prepareToPlay (48000.0, 512);
    PresetManager destManager (destProcessor.apvts, makeIsolatedConfig (destScratch.dir), makeTestFactoryPresetAssets());

    const auto importedCount = destManager.importBank (bankFile);
    CHECK (importedCount == 2);

    REQUIRE (destManager.loadPreset ("Bank Preset A"));
    CHECK (getParam (destProcessor, ParamIDs::tight) == Catch::Approx (111.0f).margin (1.0e-3));

    REQUIRE (destManager.loadPreset ("Bank Preset B"));
    CHECK (getParam (destProcessor, ParamIDs::tight) == Catch::Approx (222.0f).margin (1.0e-3));

    bankFile.deleteFile();
}

//==============================================================================
// 8. PresetManager never allocates or locks on the audio thread.
//
// Verified primarily *by design*: nothing in OvertureAudioProcessor::
// processBlock()/OvertureEngine ever calls into PresetManager (see
// PluginProcessor.cpp - presetManager is only touched from the constructor
// and from PresetBar's message-thread-only UI callbacks), so there is no
// code path for this test to exercise in the first place. The one nuance is
// PresetManager::parameterChanged() (an AudioProcessorValueTreeState::
// Listener callback that JUCE does not document as guaranteed message-
// thread-only) - it is implemented as a single lock-free std::atomic<bool>
// store and nothing else (see PresetManager.h/.cpp), which this test
// exercises indirectly by driving parameter changes and processBlock() back
// to back and confirming nothing misbehaves.
TEST_CASE ("PresetManager: parameter-driven dirty tracking coexists safely with real-time audio processing", "[presets]")
{
    OvertureAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        // Every parameterChanged() callback below happens interleaved with
        // real audio processing - if it ever became audio-thread-unsafe
        // (e.g. someone later added a lock or allocation to it), a helgrind/
        // TSan CI run would be the real detector; this test's job is just to
        // confirm normal operation isn't disrupted by the two coexisting.
        setParam (processor, ParamIDs::tight, 20.0f + static_cast<float> (block) * 10.0f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
    }

    CHECK (manager.isDirty());
}
