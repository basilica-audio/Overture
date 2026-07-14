#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace
{
    // True logarithmic (base-10) mapping for frequency parameters, so slider/
    // knob travel spends equal space per octave rather than per Hz. Uses
    // juce::mapToLog10/mapFromLog10 rather than NormalisableRange's built-in
    // power-law skew, which only approximates a log curve.
    juce::NormalisableRange<float> makeLogFrequencyRange (float minHz, float maxHz)
    {
        return juce::NormalisableRange<float> (
            minHz,
            maxHz,
            [] (float rangeStart, float rangeEnd, float normalised)
            { return juce::mapToLog10 (normalised, rangeStart, rangeEnd); },
            [] (float rangeStart, float rangeEnd, float value)
            { return juce::mapFromLog10 (value, rangeStart, rangeEnd); });
    }
}

namespace tbst
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Tight: high-pass pre-emphasis, 20-400 Hz, default 130 Hz - the
        // "808 boost" tightening knob that strips low end before the
        // clipper. 130 Hz sits close to the classic "808-mod" HPF corner
        // used ahead of a high-gain amp: high enough to keep palm mutes on
        // drop-tuned guitars tight, low enough to leave the fundamental of
        // open/low chords intact.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::tight, 1 },
            "Tight",
            makeLogFrequencyRange (20.0f, 400.0f),
            130.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // Drive: gain into the oversampled clipper (voicing selected via
        // ParamIDs::voicing). Default 8 dB: a boost stage run in front of an
        // already-driven amp typically only needs a modest push rather than
        // heavy clipper-generated distortion of its own - the amp's own
        // gain stage does the rest.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::drive, 1 },
            "Drive",
            juce::NormalisableRange<float> (0.0f, 40.0f, 0.01f),
            8.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Tone: post-clip low-pass tilt (4th-order Butterworth, see
        // OvertureEngine), 1-8 kHz, default 6 kHz. Left comparatively bright
        // by default so the amp's own tone stack - not this pre-clip
        // tightening stage - handles final top-end voicing.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::tone, 1 },
            "Tone",
            makeLogFrequencyRange (1000.0f, 8000.0f),
            6000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // Level: output trim.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::level, 1 },
            "Level",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Mix: dry/wet. Default 100% (fully wet) - a boost/overdrive pedal is
        // normally run fully in the signal path, not blended.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::mix, 1 },
            "Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Bypass: host-visible soft bypass (see ParamIDs::bypass and
        // OvertureAudioProcessor::getBypassParameter()).
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::bypass, 1 },
            "Bypass",
            false));

        //======================================================================
        // Voicing: selects the clipper nonlinearity (src/dsp/ClipperVoicing.h).
        // Default index 0 (Asymmetric) matches the original v0.1 behaviour.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::voicing, 1 },
            "Voicing",
            juce::StringArray { "Asymmetric", "Soft Symmetric", "Hard Clip" },
            0));

        //======================================================================
        // Oversampling: 2x/4x/8x. Default index 1 (4x) matches the fixed
        // factor the v0.1 engine always used. Takes effect on the next
        // prepareToPlay() call rather than instantaneously - see
        // OvertureEngine::setOversamplingFactorPow2().
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::oversampling, 1 },
            "Oversampling",
            juce::StringArray { "2x", "4x", "8x" },
            1));

        return layout;
    }
}
