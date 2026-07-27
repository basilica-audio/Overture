#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/OvertureEngine.h"
#include "presets/PresetManager.h"

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

    // Version of the APVTS state layout written by getStateInformation(),
    // stored as a "stateSchema" attribute on the saved XML's root element.
    //
    //   (absent)          v0.1.0 if a "tone" PARAM node is present,
    //                     v0.2.0 otherwise
    //   "3"               v0.3.0 (five new parameters, all neutral by
    //                     default, so no value rewriting is needed on load -
    //                     the attribute exists so future migrations can
    //                     branch on it deterministically instead of
    //                     sniffing for the presence of individual params)
    //
    // See setStateInformation() and tests/StateTests.cpp (T-S1/T-S4).
    static constexpr const char* stateSchemaAttribute = "stateSchema";
    static constexpr int currentStateSchemaVersion = 3;

    juce::AudioProcessorValueTreeState apvts;

    // M2 preset system (.scaffold/specs/preset-system-m2.md,
    // src/presets/PresetManager.h). Constructed after apvts (its
    // constructor registers APVTS parameter listeners) and public so
    // OvertureAudioProcessorEditor's PresetBar can talk to it directly - the
    // same "processor owns it, editor references it" pattern apvts itself
    // already uses.
    basilica::presets::PresetManager presetManager;

private:
    OvertureEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* tightHz = nullptr;
    std::atomic<float>* driveDb = nullptr;
    std::atomic<float>* biteAmountPercent = nullptr;
    std::atomic<float>* kneeSoftenPercent = nullptr;
    std::atomic<float>* asymmetryAmountPercent = nullptr;
    std::atomic<float>* biteTiltPercent = nullptr;
    std::atomic<float>* levelDb = nullptr;
    std::atomic<float>* mixPercent = nullptr;
    std::atomic<float>* bypassFlag = nullptr;
    std::atomic<float>* voicingChoice = nullptr;
    std::atomic<float>* oversamplingChoice = nullptr;

    // v0.3.0 additions (see src/params/ParameterIds.h). All default to
    // neutral values, so a v0.2.0 session - whose saved XML carries none of
    // these PARAM nodes at all - restores with the gate off, Classic clip
    // quality and the legacy Drive knee response, i.e. bit-identical audio.
    std::atomic<float>* gateFlag = nullptr;
    std::atomic<float>* gateThresholdDb = nullptr;
    std::atomic<float>* gateReleaseChoice = nullptr;
    std::atomic<float>* kneeResponseChoice = nullptr;
    std::atomic<float>* clipQualityChoice = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvertureAudioProcessor)
};
