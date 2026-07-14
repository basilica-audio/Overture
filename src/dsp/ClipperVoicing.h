#pragma once

#include "AsymSoftClipper.h"

#include <algorithm>
#include <cmath>

// The full set of clipper "voicings" Overture's Drive stage can select
// between (see ParamIDs::voicing and OvertureEngine::setClipperVoicing()).
// Every voicing is pure, allocation-free, and stateless, matching
// AsymSoftClipper's design so each is directly unit-testable in isolation
// (see tests/ClipperVoicingTests.cpp) and safe to call per-sample from the
// audio thread inside the 4x oversampled block.
//
// FROZEN AS OF THE v0.1 VOICING LAYOUT: these enum values are persisted as
// the "voicing" APVTS choice parameter's index (juce::AudioParameterChoice
// stores the selected item index, and that index is what ends up in saved
// state/presets) - append new voicings at the end, never reorder or remove
// an existing one, or a saved session would silently switch to a different
// clipper on load.
enum class ClipperVoicing
{
    // AsymSoftClipper: tanh(x + a) - tanh(a). Single-ended/op-amp-style
    // biased soft clip; the original v0.1 "808 boost" voicing and the
    // default for backward compatibility with existing sessions/presets.
    asymmetric = 0,

    // Symmetric (odd, unbiased) tanh soft clip - push-pull/fuzz-style
    // saturation with no DC/even-harmonic bias, for a smoother, more
    // "amp-like" overdrive than the asymmetric voicing.
    softSymmetric = 1,

    // Hard clamp to +/-1 - op-amp comparator/RAT-style hard clipping.
    // Sharper corners than either tanh voicing, so it produces more
    // high-order odd harmonics; still run inside the oversampled block so
    // those corners don't alias.
    hardClip = 2,
};

// Symmetric (odd, unbiased) tanh soft clipper: y = tanh(x). Recovers the
// zero-asymmetry case of AsymSoftClipper without paying for the (here,
// unused) bias-subtraction term.
namespace SoftSymmetricClipper
{
    inline float processSample (float x) noexcept
    {
        return std::tanh (x);
    }
}

// Hard clamp to +/-1. The simplest possible nonlinearity: no soft knee, so
// it is the brightest/most aggressive of the three voicings.
namespace HardClipper
{
    inline float processSample (float x) noexcept
    {
        return std::clamp (x, -1.0f, 1.0f);
    }
}

namespace ClipperVoicings
{
    // Dispatches a single sample to the selected voicing. `asymmetry` is
    // only meaningful for the asymmetric voicing; the other two ignore it.
    // The switch is expected to be invariant for the duration of a whole
    // process() call (voicing is only changed between blocks, never
    // mid-block), so branch prediction makes this dispatch effectively free
    // even called once per (oversampled) sample.
    inline float processSample (float x, ClipperVoicing voicing, float asymmetry) noexcept
    {
        switch (voicing)
        {
            case ClipperVoicing::softSymmetric: return SoftSymmetricClipper::processSample (x);
            case ClipperVoicing::hardClip:      return HardClipper::processSample (x);
            case ClipperVoicing::asymmetric:
            default:                             return AsymSoftClipper::processSample (x, asymmetry);
        }
    }
}
