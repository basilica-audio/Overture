#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Overture. See docs/architecture.md for the corresponding signal-flow
// diagram and docs/design-brief.md for the v0.2.0 rework this file
// implements.
//
// FROZEN AS OF THE v0.1 PARAMETER LAYOUT:
// Parameter IDs below must NEVER change once shipped - saved sessions and
// presets persist the APVTS state keyed by these string IDs, and renaming or
// removing one would silently break every user's saved state. Ranges,
// defaults, and skew MAY still be refined during voicing/tuning milestones;
// only the IDs themselves are frozen.
namespace ParamIDs
{
    // "Tight" high-pass pre-emphasis: strips low end before the clipper so
    // palm mutes stay tight instead of farting out into the gain stage.
    // Structurally unchanged in v0.2.0 - only the default value moved
    // (130 -> 100 Hz, see docs/design-brief.md's Tight section).
    inline constexpr auto tight = "tight";

    // Input gain into the oversampled clipper. Structurally unchanged in
    // v0.2.0 - only the default value moved (8 -> 3 dB, see
    // docs/design-brief.md's Drive section).
    inline constexpr auto drive = "drive";

    // RETIRED as of v0.2.0 - kept as a string constant ONLY so
    // OvertureAudioProcessor::setStateInformation() can recognise an old
    // (v0.1) saved session's "tone" value during its tolerant, lossy
    // migration into `biteTilt` (see docs/design-brief.md's "Migration"
    // section and PluginProcessor.cpp). NOT registered in
    // ParameterLayout.cpp - do not reintroduce it as a live parameter; the
    // post-clip tone control is now the bidirectional `biteTilt` below.
    inline constexpr auto tone = "tone";

    // Output trim, applied after the post-clip Bite Tilt stage and before
    // the dry/wet mix. Unchanged in v0.2.0.
    inline constexpr auto level = "level";

    // Dry/wet mix. At 0% the plugin is a delay-compensated passthrough of
    // the input (see OvertureEngine's DryWetMixer usage). Unchanged in
    // v0.2.0.
    inline constexpr auto mix = "mix";

    // Host-visible soft bypass: internally forces the wet chain's effective
    // mix to 0% rather than skipping processing outright, so the reported
    // oversampling latency (and therefore host plugin-delay-compensation)
    // stays valid and glitch-free while bypassed. See
    // OvertureAudioProcessor::getBypassParameter(). Unchanged in v0.2.0.
    inline constexpr auto bypass = "bypass";

    // Clipper voicing: selects between the asymmetric (default, v0.1),
    // symmetric soft, and hard-clip nonlinearities. Indexes into the
    // ClipperVoicing enum (src/dsp/ClipperVoicing.h) - see that file's
    // frozen-enum-value contract. Unchanged (and its indices remain frozen)
    // in v0.2.0 per docs/design-brief.md.
    inline constexpr auto voicing = "voicing";

    // Oversampling factor (2x/4x/8x). Reconstructing the internal
    // oversampler allocates, so a change here only takes effect on the
    // next prepareToPlay() call, not instantaneously mid-stream - see
    // OvertureEngine::setOversamplingFactorPow2(). Unchanged in v0.2.0.
    inline constexpr auto oversampling = "oversampling";

    //======================================================================
    // New in v0.2.0 - see docs/design-brief.md for the full mechanics and
    // sourcing of each control below.

    // Frequency-dependent gain INSIDE the drive-to-clipper gain path (a
    // fixed ~700 Hz low-shelf reducing the drive fed to the clipper below
    // the shelf, scaled by this control): 0% is a full backward-compatible
    // no-op (bit-identical to v0.1's flat-gain clipper drive), 100% is the
    // full "bass is clipped less than treble" reference-circuit-style
    // frequency-selective drive. See ClipperVoicing.h/OvertureEngine.cpp.
    inline constexpr auto biteAmount = "biteAmount";

    // Drive-dependent knee softening: blends each voicing's fixed-shape
    // transfer function toward a softer-kneed variant, more pronounced the
    // harder the clipper is being driven. 0% is a full backward-compatible
    // no-op (bit-identical to v0.1's fixed-knee voicings at every Drive
    // level) - see src/dsp/KneeSoftening.h.
    inline constexpr auto kneeSoften = "kneeSoften";

    // Exposes the Asymmetric voicing's internal bias `a` (fixed at 0.2 in
    // v0.1) as a 0-100% control mapping to `a` in 0.0-0.5. Only meaningful
    // for the Asymmetric voicing (Soft Symmetric/Hard Clip ignore it, as in
    // v0.1). Default 40% reproduces v0.1's fixed a=0.2 exactly.
    inline constexpr auto asymmetryAmount = "asymmetryAmount";

    // Post-clip bidirectional tilt (replaces v0.1's cut-only `tone` LPF):
    // a shelf anchored at a fixed ~3 kHz corner, -100%..+100%, default 0%
    // (flat/no-op). Negative values darken (subsuming v0.1's entire Tone
    // cut range), positive values brighten - a capability v0.1 entirely
    // lacked. See docs/design-brief.md's "Bite" section for the migration
    // rule mapping an old `tone` value into an equivalent `biteTilt`
    // position.
    inline constexpr auto biteTilt = "biteTilt";

    //======================================================================
    // New in v0.3.0. Every one of these defaults to a NEUTRAL value, so a
    // v0.2.0 session or preset that carries none of them loads and sounds
    // bit-identical (asserted in tests/StateTests.cpp T-S1/T-S2 and
    // tests/PresetManagerTests.cpp T-S3). See the v0.3.0 brief SS4 and
    // docs/architecture.md for the state-schema contract.
    //
    // v0.3.0 ships these WITHOUT dedicated editor controls: they are fully
    // host-automatable and reachable through the host's generic parameter
    // view, and photoreal on-screen controls arrive with the M3 GUI (see
    // docs/manual.md). The Voicing combo's new fourth entry is the sole
    // editor-visible change and appears automatically, because the editor
    // populates its combos from AudioParameterChoice::getAllValueStrings().

    // Built-in noise gate on/off. Default OFF - the whole v0.2.0 signal
    // path is untouched while this is false (src/dsp/NoiseGate.h).
    inline constexpr auto gate = "gate";

    // Gate opening threshold in dBFS (mean-square detector, so a full-scale
    // sine reads -3 dB). The closing threshold sits a fixed 4 dB below it
    // (hysteresis) - see NoiseGate::hysteresisDb.
    inline constexpr auto gateThreshold = "gateThreshold";

    // Gate release behaviour: Auto (program-dependent dual-envelope "TVP"
    // release, the default), Fast (fixed 800 dB/s), Slow (fixed 60 dB/s).
    // Indexes NoiseGate::ReleaseMode.
    inline constexpr auto gateRelease = "gateRelease";

    // How Knee Soften's intensity is derived: Drive (default, the v0.2.0
    // open-loop lastDriveDb/40 proxy - bit-identical legacy behaviour) or
    // Signal (an envelope follower on the oversampled clipper input). See
    // src/dsp/KneeSoftening.h.
    inline constexpr auto kneeResponse = "kneeResponse";

    // Clipper quality for the three MEMORYLESS voicings: Classic (default,
    // the bit-identical v0.2.0 path) or Enhanced (first-order antiderivative
    // anti-aliasing plus a 5 Hz DC blocker - src/dsp/AdaaWaveshaper.h,
    // src/dsp/DcBlocker.h). A configuration choice, not a performance
    // control. Has no effect on the Feedback voicing, which is a circuit
    // solver rather than a transfer curve (and always runs the DC blocker).
    inline constexpr auto clipQuality = "clipQuality";
}
