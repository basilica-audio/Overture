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
        // Tight: high-pass pre-emphasis, 20-400 Hz, default 100 Hz (v0.2.0:
        // was 130 Hz) - the "808 boost" tightening knob that strips low end
        // before the clipper. 100 Hz is the midpoint of the sourced 80-120 Hz
        // "cutting everything below 80-120Hz" workflow sweet spot documented
        // in docs/research-notes.md SS4, rather than v0.1's reasoned-but-
        // unsourced 130 Hz. Range/structure unchanged from v0.1.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::tight, 1 },
            "Tight",
            makeLogFrequencyRange (20.0f, 400.0f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // Drive: gain into the oversampled clipper (voicing selected via
        // ParamIDs::voicing). Default 3 dB (v0.2.0: was 8 dB) - the
        // best-documented canonical workflow for this exact "tight boost in
        // front of a high-gain amp" technique is clipper drive "at or near
        // zero" with Level doing the pushing (docs/research-notes.md SS4:
        // Misha Mansoor's documented approach; Horizon Devices' own Precision
        // Drive manual: "start with drive near zero... slowly turn up to
        // around 1-2 [of 10]"). 3 dB is a small, non-zero push that keeps the
        // clipper audibly alive while sitting in that researched "mostly
        // clean push" region. Range unchanged (0-40 dB comfortably covers the
        // reference circuit's own ~21.5-41.5 dB gain-stage range).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::drive, 1 },
            "Drive",
            juce::NormalisableRange<float> (0.0f, 40.0f, 0.01f),
            3.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Bite: frequency-dependent gain inside the drive-to-clipper gain
        // path (new in v0.2.0 - see docs/design-brief.md's "bite_amount"
        // section and OvertureEngine.cpp). 0-100%, default 65%. At 0% the
        // clipper's drive gain is flat with frequency - a full
        // backward-compatible no-op, bit-identical to v0.1's clipper (see
        // tests/EngineTests.cpp's backward-compatibility null tests).
        // Default 65% (not 100%) is a reasoned starting compromise, not
        // sourced to a specific number - flagged as such in the brief.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::biteAmount, 1 },
            "Bite",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            65.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Knee Soften: drive-dependent knee softening (new in v0.2.0 - see
        // docs/design-brief.md's "knee_soften" section and
        // src/dsp/KneeSoftening.h). 0-100%, default 40%. At 0% all three
        // voicings behave exactly as in v0.1 (bit-identical transfer
        // functions at every Drive level) - see
        // tests/EngineTests.cpp/ClipperVoicingTests.cpp. Default 40% is
        // reasoned (moderate, audible but not smoothing away Hard Clip's
        // identity), not sourced to a specific number.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::kneeSoften, 1 },
            "Knee Soften",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            40.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Asymmetry: exposes the Asymmetric voicing's internal bias `a`
        // (fixed at 0.2 in v0.1) as a 0-100% control mapping to `a` in
        // 0.0-0.5 (new in v0.2.0 - see docs/design-brief.md's
        // "asymmetry_amount" section). Default 40% -> a = 0.2, reproducing
        // v0.1's fixed default exactly. Only meaningful for the Asymmetric
        // voicing - Soft Symmetric/Hard Clip ignore it, as in v0.1.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::asymmetryAmount, 1 },
            "Asymmetry",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            40.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // Bite Tilt: post-clip bidirectional tilt anchored at a fixed ~3 kHz
        // corner (new in v0.2.0, replaces v0.1's cut-only `tone` 1-8 kHz
        // low-pass - see docs/design-brief.md's "Bite" section and
        // OvertureEngine.cpp). -100%..+100%, default 0% (flat/no-op).
        // Negative values darken (subsuming v0.1's entire Tone cut range),
        // positive values brighten - a capability v0.1 entirely lacked. An
        // old v0.1 session's `tone` value is lossily migrated into an
        // equivalent negative biteTilt position on load - see
        // OvertureAudioProcessor::setStateInformation().
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::biteTilt, 1 },
            "Bite Tilt",
            juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

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
        // Enum indices are FROZEN - see ClipperVoicing.h.
        //
        // v0.3.0 APPENDS a fourth entry, "Feedback" (index 3): the
        // circuit-solved op-amp/diode feedback clipper
        // (src/dsp/FeedbackClipperStage.h). Indices 0-2 are untouched, so
        // saved sessions and presets - which persist the denormalised choice
        // INDEX, not a normalised float - keep selecting exactly the voicing
        // they always did. A pre-existing host AUTOMATION lane, which does
        // record normalised values, is the one documented exception: a lane
        // holding 1.0 used to mean "Hard Clip" (2 of 2) and now means
        // "Feedback" (3 of 3). Voicing is a discrete configuration control
        // rather than a performance control, and CHANGELOG.md carries the
        // compatibility note - see the v0.3.0 brief SS7 R1.
        //
        // The v0.2 editor builds its combo items from
        // AudioParameterChoice::getAllValueStrings(), so this fourth entry
        // appears on screen with no editor change at all (v0.3.0 ships zero
        // PluginEditor edits - the M3 photoreal GUI branch owns those).
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::voicing, 1 },
            "Voicing",
            juce::StringArray { "Asymmetric", "Soft Symmetric", "Hard Clip", "Feedback" },
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

        //======================================================================
        // v0.3.0 additions. EVERY default below is neutral, i.e. chosen so
        // that a project saved with v0.2.0 - which carries none of these
        // PARAM nodes and therefore gets the defaults - produces
        // bit-identical audio (tests/StateTests.cpp T-S1/T-S2).

        //======================================================================
        // Gate: the built-in noise gate's master on/off (new in v0.3.0 - see
        // src/dsp/NoiseGate.h and docs/manual.md). Default OFF: the entire
        // gate - detector, sidechain filters and gain stage - is skipped
        // while this is false, so the v0.2.0 signal path is untouched rather
        // than merely multiplied by unity.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::gate, 1 },
            "Gate",
            false));

        //======================================================================
        // Gate Threshold: the opening threshold, -80..-20 dB. The gate's
        // detector is a 5 ms mean-square follower, so a full-scale sine
        // reads -3 dB and a typical high-output DI's noise floor sits around
        // -60..-45 dB. Default -50 dB is below any realistic playing level
        // (so the gate is inaudible even if switched on blind) while still
        // above a hot single-coil's hum. The closing threshold tracks 4 dB
        // below this (NoiseGate::hysteresisDb).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::gateThreshold, 1 },
            "Gate Threshold",
            juce::NormalisableRange<float> (-80.0f, -20.0f, 0.1f),
            -50.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Gate Release: Auto (index 0, default) runs the program-dependent
        // dual-envelope release - a staccato mute closes the gate at
        // 1000 dB/s while a ringing chord is released at its own measured
        // decay rate, so one setting covers both. Fast/Slow are the fixed
        // 800 / 60 dB/s escape hatches. Order matches
        // NoiseGate::ReleaseMode.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::gateRelease, 1 },
            "Gate Release",
            juce::StringArray { "Auto", "Fast", "Slow" },
            0));

        //======================================================================
        // Knee Response: where Knee Soften's intensity comes from. "Drive"
        // (index 0, default) is v0.2.0's open-loop lastDriveDb/40 proxy and
        // is bit-identical to it; "Signal" derives the intensity from an
        // envelope follower on the oversampled clipper input, so the knee
        // responds to how hard the circuit is ACTUALLY being hit rather than
        // to where the knob sits. See src/dsp/KneeSoftening.h.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::kneeResponse, 1 },
            "Knee Response",
            juce::StringArray { "Drive", "Signal" },
            0));

        //======================================================================
        // Clip Quality: "Classic" (index 0, default) is the exact v0.2.0
        // clipper path, bit for bit. "Enhanced" adds first-order
        // antiderivative anti-aliasing to the three memoryless voicings plus
        // a 5 Hz DC blocker on the clipper output - measurably ~20 dB less
        // alias energy at 2x oversampling (tests/AdaaTests.cpp T-A1) with
        // the harmonic spectrum unchanged (T-A2). A configuration choice,
        // not a performance control: it is deliberately NOT smoothed or
        // crossfaded, and it has no effect on the Feedback voicing, which is
        // a circuit solver rather than a transfer curve.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::clipQuality, 1 },
            "Clip Quality",
            juce::StringArray { "Classic", "Enhanced" },
            0));

        return layout;
    }
}
