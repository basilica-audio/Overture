#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <BinaryData.h>

//==============================================================================
namespace
{
    // The small, Overture-specific config surface PresetManager needs (see
    // src/presets/PresetManager.h's class docs) - everything else about the
    // preset system is fully generic and portable across the Basilica Audio
    // suite (see docs/preset-system-notes.md, copied from the Nave pilot).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. Always
        // "com.yvesvogl.overture" here (BUNDLE_ID in CMakeLists.txt),
        // matching the "plugin" field baked into every
        // presets/factory/*.json file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h).
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::classicBoost_json, BinaryData::classicBoost_jsonSize },
            { BinaryData::cleanPush_json, BinaryData::cleanPush_jsonSize },
            { BinaryData::dropTuneTight_json, BinaryData::dropTuneTight_jsonSize },
            { BinaryData::smoothPush_json, BinaryData::smoothPush_jsonSize },
            { BinaryData::ownDistortion_json, BinaryData::ownDistortion_jsonSize },
            { BinaryData::fuzzAdjacentLead_json, BinaryData::fuzzAdjacentLead_jsonSize },
            { BinaryData::parallelGrit_json, BinaryData::parallelGrit_jsonSize },
            { BinaryData::deFizzCleanup_json, BinaryData::deFizzCleanup_jsonSize },
            { BinaryData::tightRhythmGate_json, BinaryData::tightRhythmGate_jsonSize },
            { BinaryData::circuitDrive_json, BinaryData::circuitDrive_jsonSize },
        };
    }
}

namespace
{
    // The v0.3.0 parameter push shared by prepareToPlay() and processBlock().
    // Kept in one place so the engine can never be prepared with one set of
    // modes and then processed with another (the Feedback voicing and the
    // gate both carry state that is seeded at prepare time).
    void pushV3Parameters (OvertureEngine& engine,
                           float gateOn,
                           float thresholdDb,
                           float releaseChoice,
                           float kneeResponse,
                           float clipQuality)
    {
        engine.setGateEnabled (gateOn >= 0.5f);
        engine.setGateThresholdDb (thresholdDb);
        engine.setGateReleaseMode (static_cast<basilica::dsp::NoiseGate::ReleaseMode> (
            juce::jlimit (0, 2, static_cast<int> (releaseChoice))));
        engine.setKneeResponseMode (static_cast<KneeResponseMode> (juce::jlimit (0, 1, static_cast<int> (kneeResponse))));
        engine.setClipQualityMode (static_cast<ClipQualityMode> (juce::jlimit (0, 1, static_cast<int> (clipQuality))));
    }
}

//==============================================================================
OvertureAudioProcessor::OvertureAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
{
    tightHz = apvts.getRawParameterValue (ParamIDs::tight);
    driveDb = apvts.getRawParameterValue (ParamIDs::drive);
    biteAmountPercent = apvts.getRawParameterValue (ParamIDs::biteAmount);
    kneeSoftenPercent = apvts.getRawParameterValue (ParamIDs::kneeSoften);
    asymmetryAmountPercent = apvts.getRawParameterValue (ParamIDs::asymmetryAmount);
    biteTiltPercent = apvts.getRawParameterValue (ParamIDs::biteTilt);
    levelDb = apvts.getRawParameterValue (ParamIDs::level);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);
    bypassFlag = apvts.getRawParameterValue (ParamIDs::bypass);
    voicingChoice = apvts.getRawParameterValue (ParamIDs::voicing);
    oversamplingChoice = apvts.getRawParameterValue (ParamIDs::oversampling);
    gateFlag = apvts.getRawParameterValue (ParamIDs::gate);
    gateThresholdDb = apvts.getRawParameterValue (ParamIDs::gateThreshold);
    gateReleaseChoice = apvts.getRawParameterValue (ParamIDs::gateRelease);
    kneeResponseChoice = apvts.getRawParameterValue (ParamIDs::kneeResponse);
    clipQualityChoice = apvts.getRawParameterValue (ParamIDs::clipQuality);

    jassert (tightHz != nullptr);
    jassert (driveDb != nullptr);
    jassert (biteAmountPercent != nullptr);
    jassert (kneeSoftenPercent != nullptr);
    jassert (asymmetryAmountPercent != nullptr);
    jassert (biteTiltPercent != nullptr);
    jassert (levelDb != nullptr);
    jassert (mixPercent != nullptr);
    jassert (bypassFlag != nullptr);
    jassert (voicingChoice != nullptr);
    jassert (oversamplingChoice != nullptr);
    jassert (gateFlag != nullptr);
    jassert (gateThresholdDb != nullptr);
    jassert (gateReleaseChoice != nullptr);
    jassert (kneeResponseChoice != nullptr);
    jassert (clipQualityChoice != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset (there isn't one here - see docs/presets.md) > the
    // ParameterLayout defaults apvts was just constructed with above (see
    // PresetManager::applyStartupDefault()'s docs).
    presetManager.applyStartupDefault();
}

OvertureAudioProcessor::~OvertureAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout OvertureAudioProcessor::createParameterLayout()
{
    return tbst::createParameterLayout();
}

//==============================================================================
const juce::String OvertureAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool OvertureAudioProcessor::acceptsMidi() const
{
    return false;
}

bool OvertureAudioProcessor::producesMidi() const
{
    return false;
}

bool OvertureAudioProcessor::isMidiEffect() const
{
    return false;
}

double OvertureAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int OvertureAudioProcessor::getNumPrograms()
{
    return 1;
}

int OvertureAudioProcessor::getCurrentProgram()
{
    return 0;
}

void OvertureAudioProcessor::setCurrentProgram (int)
{
}

const juce::String OvertureAudioProcessor::getProgramName (int)
{
    return {};
}

void OvertureAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void OvertureAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine's parameters from the current APVTS state before
    // prepare() primes the filter coefficients, so the very first block
    // after prepareToPlay() already reflects the host/session's actual
    // parameter values rather than the engine's built-in defaults.
    engine.setTightFrequencyHz (tightHz->load (std::memory_order_relaxed));
    engine.setDriveDb (driveDb->load (std::memory_order_relaxed));
    engine.setBiteAmountPercent (biteAmountPercent->load (std::memory_order_relaxed));
    engine.setKneeSoftenPercent (kneeSoftenPercent->load (std::memory_order_relaxed));
    engine.setAsymmetryAmountPercent (asymmetryAmountPercent->load (std::memory_order_relaxed));
    engine.setBiteTiltPercent (biteTiltPercent->load (std::memory_order_relaxed));
    engine.setLevelDb (levelDb->load (std::memory_order_relaxed));
    engine.setMixProportion (mixPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setClipperVoicing (static_cast<ClipperVoicing> (static_cast<int> (voicingChoice->load (std::memory_order_relaxed))));

    pushV3Parameters (engine,
                      gateFlag->load (std::memory_order_relaxed),
                      gateThresholdDb->load (std::memory_order_relaxed),
                      gateReleaseChoice->load (std::memory_order_relaxed),
                      kneeResponseChoice->load (std::memory_order_relaxed),
                      clipQualityChoice->load (std::memory_order_relaxed));

    // Oversampling factor: reconstructing the internal juce::dsp::Oversampling
    // instance allocates, so it is only ever (re)constructed here, inside
    // engine.prepare() below - never from processBlock(). This means a
    // mid-stream change to the Oversampling parameter only takes effect the
    // next time the host calls prepareToPlay() (transport stop/start,
    // sample-rate change, etc.), not instantaneously - documented in
    // docs/manual.md.
    const auto oversamplingChoiceIndex = static_cast<int> (oversamplingChoice->load (std::memory_order_relaxed));
    engine.setOversamplingFactorPow2 (oversamplingChoiceIndex + 1); // index 0/1/2 -> 2x/4x/8x

    engine.prepare (spec);

    // Oversampling (4x by default, applied around the clipper) is the only
    // source of reported latency; the dry path is compensated against it
    // internally by OvertureEngine's DryWetMixer (see docs/architecture.md).
    setLatencySamples (engine.getLatencySamples());
}

void OvertureAudioProcessor::releaseResources()
{
}

void OvertureAudioProcessor::reset()
{
    engine.reset();
}

bool OvertureAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    return true;
}

void OvertureAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    engine.setTightFrequencyHz (tightHz->load (std::memory_order_relaxed));
    engine.setDriveDb (driveDb->load (std::memory_order_relaxed));
    engine.setBiteAmountPercent (biteAmountPercent->load (std::memory_order_relaxed));
    engine.setKneeSoftenPercent (kneeSoftenPercent->load (std::memory_order_relaxed));
    engine.setAsymmetryAmountPercent (asymmetryAmountPercent->load (std::memory_order_relaxed));
    engine.setBiteTiltPercent (biteTiltPercent->load (std::memory_order_relaxed));
    engine.setLevelDb (levelDb->load (std::memory_order_relaxed));
    engine.setClipperVoicing (static_cast<ClipperVoicing> (static_cast<int> (voicingChoice->load (std::memory_order_relaxed))));

    pushV3Parameters (engine,
                      gateFlag->load (std::memory_order_relaxed),
                      gateThresholdDb->load (std::memory_order_relaxed),
                      gateReleaseChoice->load (std::memory_order_relaxed),
                      kneeResponseChoice->load (std::memory_order_relaxed),
                      clipQualityChoice->load (std::memory_order_relaxed));

    // Soft bypass: force the wet chain's effective mix to 0% instead of
    // skipping engine.process() outright, so the oversampler keeps running
    // and the plugin's reported latency (host PDC) stays valid and
    // glitch-free while bypassed - see ParamIDs::bypass and
    // getBypassParameter().
    const auto isBypassed = bypassFlag->load (std::memory_order_relaxed) >= 0.5f;
    const auto requestedMixProportion = mixPercent->load (std::memory_order_relaxed) * 0.01f;
    engine.setMixProportion (isBypassed ? 0.0f : requestedMixProportion);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);
}

//==============================================================================
juce::AudioProcessorParameter* OvertureAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

//==============================================================================
bool OvertureAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* OvertureAudioProcessor::createEditor()
{
    return new OvertureAudioProcessorEditor (*this);
}

//==============================================================================
void OvertureAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // Stamp the state-schema version (see PluginProcessor.h). v0.3.0 needs
    // no value rewriting on load - every new parameter's default IS the
    // neutral, v0.2.0-equivalent value - so this attribute exists purely so
    // a future migration can branch on an explicit version instead of
    // sniffing for individual PARAM nodes.
    xml->setAttribute (stateSchemaAttribute, juce::String (currentStateSchemaVersion));

    copyXmlToBinary (*xml, destData);
}

void OvertureAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState == nullptr || ! xmlState->hasTagName (apvts.state.getType()))
        return;

    // v0.2.0 tolerant, lossy migration of a v0.1-only "tone" (cut-only,
    // 1-8 kHz low-pass) session value into the new bidirectional
    // "biteTilt" parameter (docs/design-brief.md's "Migration" section;
    // ParamIDs::tone's docs). Detected BEFORE apvts.replaceState() below,
    // since a v0.1 saved XML tree has no "biteTilt" PARAM node at all for
    // replaceState() to apply - APVTS itself silently ignores PARAM nodes
    // that don't match a currently-registered parameter ID (the same
    // tolerant-import behaviour the M2 preset system's JSON format uses),
    // which is what makes loading an old session safe but insufficient on
    // its own to recover the old Tone value's intent.
    float migratedBiteTiltPercent = 0.0f;
    bool shouldApplyMigratedBiteTilt = false;

    for (auto* child : xmlState->getChildIterator())
    {
        if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == juce::String (ParamIDs::tone))
        {
            // v0.1's Tone range was 1000-8000 Hz, cut-only (higher Hz =
            // more open/brighter, per docs/design-brief.md). Linear map:
            // 1000 Hz (v0.1's fully-closed/darkest Tone) -> -100%
            // (maximally negative/dark biteTilt); 8000 Hz (v0.1's
            // fully-open/brightest Tone) -> 0% (flat) - a lossy,
            // best-effort equivalence, not a mathematically exact one, per
            // the brief's own "Migration" section.
            const auto legacyToneHz = juce::jlimit (1000.0, 8000.0, child->getDoubleAttribute ("value", 8000.0));
            migratedBiteTiltPercent = static_cast<float> (-100.0 * (8000.0 - legacyToneHz) / 7000.0);
            shouldApplyMigratedBiteTilt = true;
            break;
        }
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xmlState));

    if (shouldApplyMigratedBiteTilt)
        if (auto* biteTiltParam = apvts.getParameter (ParamIDs::biteTilt))
            biteTiltParam->setValueNotifyingHost (biteTiltParam->convertTo0to1 (migratedBiteTiltPercent));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OvertureAudioProcessor();
}
