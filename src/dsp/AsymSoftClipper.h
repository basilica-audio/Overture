#pragma once

#include <cmath>

// A single-ended, asymmetric tanh soft clipper - the nonlinearity at the
// heart of Overture's "808 boost" voicing. Pure, allocation-free, and
// stateless, so it is unit-testable in complete isolation from the rest of
// the signal chain (see tests/AsymSoftClipperTests.cpp) and safe to call
// per-sample from the audio thread.
//
// Transfer function: y = tanh(x + a) - tanh(a)
//
// Shifting the tanh curve by a fixed bias `a` before re-centring it at
// y(0) == 0 gives two distinct effects, both desired here:
//   - The positive and negative half-cycles saturate towards different
//     asymptotic ceilings (1 - tanh(a) vs -(1 + tanh(a))), i.e. genuine
//     asymmetric clipping, emulating single-ended (op-amp/diode) clipping
//     topologies rather than a symmetric fuzz.
//   - Because the curve is not globally linear, a zero-mean AC input
//     produces a small even-harmonic/DC-shifted output - the "small DC/
//     asymmetry term" called for in the brief. This is confined to the wet
//     path; the dry path used for the Mix control is untouched.
// Subtracting tanh(a) guarantees processSample(0, a) == 0 for any bias, so
// the clipper never injects a constant DC offset into silence.
namespace AsymSoftClipper
{
    inline float processSample (float x, float asymmetry) noexcept
    {
        return std::tanh (x + asymmetry) - std::tanh (asymmetry);
    }
}
