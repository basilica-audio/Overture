#pragma once

#include <cmath>

// Drive-dependent knee softening (Overture v0.2.0 - see
// docs/design-brief.md's "knee_soften" section). Pure, allocation-free, and
// stateless, so it is unit-testable in isolation (see
// tests/KneeSofteningTests.cpp) and safe to call per-sample from the audio
// thread inside the oversampled block, directly after the voicing dispatch
// (ClipperVoicings::processSample()).
//
// Real diode clippers get progressively softer-cornered as drive increases
// (the reference circuit's small feedback cap, per docs/research-notes.md
// SS3: "softens the corners of the clipped waveform... most noticeable when
// the drive control is maxed out"). This is modelled here as a blend, at the
// OUTPUT of the already-computed voicing transfer function, towards a
// rounded-corner variant obtained by re-applying tanh() to that same raw
// output - a simple, generic softening applicable uniformly to all three
// voicings, including Hard Clip (whose raw clamp() has literally zero knee
// at any drive level in v0.1/v0.2, which is exactly what makes it audibly
// softenable at all: tanh() rounds the sharp +/-1 corner a hard clamp
// produces).
//
// OvertureEngine::processChunk() computes `blendAmount01` as
// (kneeSoften/100) * driveIntensity01, where driveIntensity01 is a
// normalised proxy for how hard the clipper is currently being driven (see
// that file) - this is what makes the softening "drive-dependent": at a
// fixed kneeSoften > 0, a harder-driven clipper produces a larger
// blendAmount01 and therefore a softer knee, while at kneeSoften == 0 the
// blend is always exactly 0 regardless of Drive, reproducing v0.1's
// Drive-invariant fixed-knee behaviour exactly (see
// tests/ClipperVoicingTests.cpp's knee-softening backward-compatibility and
// drive-dependence tests).
// v0.3.0 addendum: `driveIntensity01` (the second factor in blendAmount01)
// is no longer necessarily the open-loop `lastDriveDb/40` proxy. With
// ParamIDs::kneeResponse = "Signal" the engine instead derives it from an
// instant-attack / 30 ms-release peak envelope taken on the OVERSAMPLED
// clipper input, so a quiet passage at Drive 40 gets a hard knee and a
// slammed input gets the soft one - the circuit behaviour the open-loop
// proxy could only approximate. The math below is unchanged; only where the
// intensity comes from moved (see KneeSoftening::intensityFromDriveDb() and
// OvertureEngine::processSubBlock()).
namespace KneeSoftening
{
    // The legacy (v0.2.0) open-loop intensity proxy, factored out of
    // OvertureEngine so both knee-response modes read from one definition
    // and the "Drive" mode stays provably identical to v0.2.0.
    // `referenceDriveDb` is the top of Drive's own range (40 dB), so the
    // proxy reaches 1.0 exactly at maximum Drive.
    inline float intensityFromDriveDb (float driveDb, float referenceDriveDb) noexcept
    {
        const auto normalised = referenceDriveDb > 0.0f ? driveDb / referenceDriveDb : 0.0f;
        return normalised < 0.0f ? 0.0f : (normalised > 1.0f ? 1.0f : normalised);
    }

    // `blendAmount01` is expected to be clamped to [0, 1] by the caller
    // (OvertureEngine::processChunk() derives it from two already-clamped
    // 0-1 quantities, so no further clamping happens here). At
    // blendAmount01 == 0 this returns `raw` unchanged; callers on the audio
    // thread skip calling this entirely in that case rather than paying for
    // the (here, harmless but wasted) extra std::tanh() call - see
    // OvertureEngine.cpp's dispatch, which is what gives the "0% = v0.1
    // bit-identical" guarantee its bit-for-bit precision rather than relying
    // on `0.0f * x == 0.0f` alone.
    inline float apply (float raw, float blendAmount01) noexcept
    {
        const auto softened = std::tanh (raw);
        return raw + blendAmount01 * (softened - raw);
    }
}
