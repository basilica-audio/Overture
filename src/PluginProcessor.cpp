#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

//==============================================================================
OvertureAudioProcessor::OvertureAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    tightHz = apvts.getRawParameterValue (ParamIDs::tight);
    driveDb = apvts.getRawParameterValue (ParamIDs::drive);
    toneHz = apvts.getRawParameterValue (ParamIDs::tone);
    levelDb = apvts.getRawParameterValue (ParamIDs::level);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);

    jassert (tightHz != nullptr);
    jassert (driveDb != nullptr);
    jassert (toneHz != nullptr);
    jassert (levelDb != nullptr);
    jassert (mixPercent != nullptr);
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
    engine.setToneFrequencyHz (toneHz->load (std::memory_order_relaxed));
    engine.setLevelDb (levelDb->load (std::memory_order_relaxed));
    engine.setMixProportion (mixPercent->load (std::memory_order_relaxed) * 0.01f);

    engine.prepare (spec);

    // Oversampling (4x, applied around the clipper) is the only source of
    // reported latency; the dry path is compensated against it internally
    // by OvertureEngine's DryWetMixer (see docs/architecture.md).
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
    engine.setToneFrequencyHz (toneHz->load (std::memory_order_relaxed));
    engine.setLevelDb (levelDb->load (std::memory_order_relaxed));
    engine.setMixProportion (mixPercent->load (std::memory_order_relaxed) * 0.01f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);
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
    copyXmlToBinary (*xml, destData);
}

void OvertureAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OvertureAudioProcessor();
}
