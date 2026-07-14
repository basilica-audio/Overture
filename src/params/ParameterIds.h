#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Overture. See docs/architecture.md for the corresponding signal-flow
// diagram.
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
    inline constexpr auto tight = "tight";

    // Input gain into the oversampled clipper.
    inline constexpr auto drive = "drive";

    // Post-clip low-pass tilt: tames fizz/aliasing-adjacent harshness from
    // the clipper without touching the fundamental.
    inline constexpr auto tone = "tone";

    // Output trim, applied after the tone stage and before the dry/wet mix.
    inline constexpr auto level = "level";

    // Dry/wet mix. At 0% the plugin is a delay-compensated passthrough of
    // the input (see OvertureEngine's DryWetMixer usage).
    inline constexpr auto mix = "mix";

    // Host-visible soft bypass: internally forces the wet chain's effective
    // mix to 0% rather than skipping processing outright, so the reported
    // oversampling latency (and therefore host plugin-delay-compensation)
    // stays valid and glitch-free while bypassed. See
    // OvertureAudioProcessor::getBypassParameter().
    inline constexpr auto bypass = "bypass";

    // Clipper voicing: selects between the asymmetric (default, v0.1),
    // symmetric soft, and hard-clip nonlinearities. Indexes into the
    // ClipperVoicing enum (src/dsp/ClipperVoicing.h) - see that file's
    // frozen-enum-value contract.
    inline constexpr auto voicing = "voicing";

    // Oversampling factor (2x/4x/8x). Reconstructing the internal
    // oversampler allocates, so a change here only takes effect on the
    // next prepareToPlay() call, not instantaneously mid-stream - see
    // OvertureEngine::setOversamplingFactorPow2().
    inline constexpr auto oversampling = "oversampling";
}
