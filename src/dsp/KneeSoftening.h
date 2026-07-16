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
namespace KneeSoftening
{
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
