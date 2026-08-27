cmake_minimum_required(VERSION 3.24)

# Must be set before the first project() call to take effect.
# CI overrides CMAKE_OSX_ARCHITECTURES explicitly for release/universal builds;
# local builds are left at the native architecture.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target" FORCE)

# JUCE's build tooling (juceaide) and some CMake find-modules it triggers
# (e.g. FindThreads) expect the C toolchain to be available; declaring it
# here at the top level avoids it being enabled implicitly deeper inside
# JUCE's CMake helpers, which has been observed to break Ninja's generate
# step (missing CMAKE_C_COMPILE_OBJECT rule variable).
project(Overture VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/CPM.cmake)

# CPMAddPackage automatically respects the CPM_SOURCE_CACHE environment
# variable if it is set, reusing a shared package cache across builds/checkouts.
if(DEFINED ENV{CPM_SOURCE_CACHE})
    message(STATUS "Overture: using CPM_SOURCE_CACHE at $ENV{CPM_SOURCE_CACHE}")
endif()

CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)

# ==============================================================================
# Plugin target
# ==============================================================================

juce_add_plugin(Overture
    COMPANY_NAME "Yves Vogl"
    BUNDLE_ID com.yvesvogl.overture
    PLUGIN_MANUFACTURER_CODE Yvsv
    PLUGIN_CODE Ovtr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Overture"
    COMPANY_WEBSITE "https://basilica-audio.github.io/website/"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Yves Vogl"
    DESCRIPTION "Tight boost and overdrive for the pre-amp tightening stage"
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    ICON_BIG "docs/assets/icon.png"
)

# ==============================================================================
# Embedded assets: M2 preset system factory presets (presets/factory/*.json)
# and the M2 i18n frame's German translation (resources/i18n/de.txt). See
# src/presets/PresetManager.h and src/presets/Localisation.h - both consume
# these purely via BinaryData:: symbols passed in from PluginProcessor.cpp/
# PluginEditor.cpp, never by including BinaryData.h themselves, so they stay
# portable across the Basilica Audio suite (copied verbatim from the Nave
# pilot - see docs/design-brief.md).
# ==============================================================================

juce_add_binary_data(OvertureBinaryData SOURCES
    presets/factory/default.json
    presets/factory/cleanPush.json
    presets/factory/classicBoost.json
    presets/factory/dropTuneTight.json
    presets/factory/smoothPush.json
    presets/factory/ownDistortion.json
    presets/factory/fuzzAdjacentLead.json
    presets/factory/parallelGrit.json
    presets/factory/deFizzCleanup.json
    presets/factory/tightRhythmGate.json
    presets/factory/circuitDrive.json
    resources/i18n/de.txt
    # M3 photoreal GUI assets (resources/gui/, copied from the suite's
    # Blender pre-render pipeline at .scaffold/gui-assets/ - see each asset
    # family's README there for provenance/verification, and
    # basilica-audio/silentium's M3 pilot for the pattern this replicates).
    # The repo is self-contained: these copies are the ones the plugin ships
    # with.
    resources/gui/knob_brass_strip_160px_128f.png
    resources/gui/knob_brass_strip_320px_128f.png
    resources/gui/toggle_brass_strip_100px_4f.png
    resources/gui/toggle_brass_strip_200px_4f.png
    resources/gui/faceplate_overture_900x600.png
    resources/gui/faceplate_overture_1800x1200.png
    # Brand icon (already the plugin's ICON_BIG; embedded here additionally
    # so the editor can draw it in the faceplate's header roundel).
    docs/assets/icon-256.png
    # Wave-3 compositional GUI (campaign 2026-08, supersedes the filmstrip
    # faceplate assets above, which stay embedded per the suite's
    # "superseded, not deleted" convention):
    #   - plate_overture.png: the accepted EMPTY family plate render
    #     (.scaffold/gui-assets/rollout-2026-07/overture/out/plate-01.png,
    #     wave-2 job 5fc01211 - see out/VERDICT.md).
    #   - sprite_*.png: extracted control sprites, copied verbatim from
    #     .scaffold/gui-assets/sprite-library/ (see its provenance.md for
    #     source render + crop rect per sprite).
    #   - layout_manifest.json: the compositional layout manifest, the
    #     single source of truth for control positions (see
    #     src/gui/LayoutManifest.h).
    resources/gui/plate_overture.png
    resources/gui/sprite_knob_brass.png
    resources/gui/sprite_selector_stepped.png
    resources/gui/sprite_toggle_up.png
    resources/gui/layout_manifest.json
    # Suite typography: EB Garamond (OFL, resources/fonts/OFL-EBGaramond.txt),
    # the same two faces the merged typography-pass siblings embed - used by
    # src/gui/PlateTypography for the engraved control lettering drawn live
    # over the plate (text is never baked into the renders).
    resources/fonts/EBGaramond-Regular.ttf
    resources/fonts/EBGaramond-SemiBold.ttf
)

# ==============================================================================
# SharedCode: an INTERFACE target carrying the plugin's sources, definitions,
# and library links so that both the plugin targets (AU/VST3/Standalone) and
# the Tests executable compile the exact same code (Pamplejuce pattern, see
# https://github.com/sudara/pamplejuce), avoiding ODR violations between them.
# ==============================================================================

add_library(SharedCode INTERFACE)

target_compile_features(SharedCode INTERFACE cxx_std_20)

file(GLOB_RECURSE OvertureSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

target_sources(SharedCode INTERFACE ${OvertureSources})

target_include_directories(SharedCode INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(SharedCode INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # AGPLv3 usage permits disabling the splash screen.
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # processBlock() holds a juce::ScopedNoDenormals, which forces FTZ/DAZ for
    # the whole audio callback, so juce_dsp's per-block snap-to-zero passes are
    # redundant work on the audio thread. juce_dsp.h (JUCE 8.0.14) documents
    # disabling the flag as safe exactly in this case. It also keeps denormal
    # tail tests honest: the library no longer masks a missing FTZ path.
    # Fleet decision: basilica-audio/Crypta#99.
    JUCE_DSP_ENABLE_SNAP_TO_ZERO=0
)

# Add any extra juce:: modules a specific plugin needs (e.g. juce_audio_formats
# for reading impulse-response files) to this list.
target_link_libraries(SharedCode INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    OvertureBinaryData
)

target_link_libraries(Overture PRIVATE SharedCode)

# ==============================================================================
# Tests
# ==============================================================================

enable_testing()
set(CTEST_OUTPUT_ON_FAILURE ON)

CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.15.2
)

file(GLOB_RECURSE OvertureTestSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")

add_executable(Tests ${OvertureTestSources})

target_compile_features(Tests PRIVATE cxx_std_20)

target_link_libraries(Tests PRIVATE SharedCode Catch2::Catch2)

# Copy over the plugin target's compile definitions (JucePlugin_* macros etc.)
# so plugin code compiles identically when built as part of the Tests binary.
target_compile_definitions(Tests PRIVATE
    $<TARGET_PROPERTY:Overture,COMPILE_DEFINITIONS>
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Absolute path to docs/, so tests/gui/EditorSnapshotTests.cpp can write
    # the committed docs/gui-preview.png directly rather than a copy step.
    OVERTURE_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs"
)

set_target_properties(Tests PROPERTIES XCODE_GENERATE_SCHEME ON)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(OvertureDiscoveryMode PRE_TEST)
else()
    set(OvertureDiscoveryMode POST_BUILD)
endif()

catch_discover_tests(Tests DISCOVERY_MODE ${OvertureDiscoveryMode})
