#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/OvertureEngine.h"

// Overture: a TS-808-style tight overdrive/boost. Signal flow lives in
// OvertureEngine (src/dsp) so it stays unit-testable independent of this
// AudioProcessor; this class is just APVTS + host plumbing around it.
class OvertureAudioProcessor final : public juce::AudioProcessor
{
public:
    OvertureAudioProcessor();
    ~OvertureAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Returns the "bypass" parameter so AU/VST3/AAX/LV2 hosts treat it as
    // native soft bypass (see ParamIDs::bypass). processBlock() checks this
    // parameter itself and forces the wet chain's effective mix to 0% while
    // bypassed, rather than this class ever implementing
    // processBlockBypassed() - that keeps the oversampler running and the
    // reported latency valid throughout, avoiding a PDC glitch on bypass
    // toggle. See JUCE 8.0.14 juce::AudioProcessor::getBypassParameter().
    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

private:
    OvertureEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* tightHz = nullptr;
    std::atomic<float>* driveDb = nullptr;
    std::atomic<float>* toneHz = nullptr;
    std::atomic<float>* levelDb = nullptr;
    std::atomic<float>* mixPercent = nullptr;
    std::atomic<float>* bypassFlag = nullptr;
    std::atomic<float>* voicingChoice = nullptr;
    std::atomic<float>* oversamplingChoice = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureAudioProcessor)
};
