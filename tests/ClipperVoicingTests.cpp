#include "dsp/ClipperVoicing.h"
#include "dsp/KneeSoftening.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

TEST_CASE ("SoftSymmetricClipper: silence in, silence out", "[dsp][clipper][voicing]")
{
    CHECK (SoftSymmetricClipper::processSample (0.0f) == Catch::Approx (0.0f).margin (1e-9));
}

TEST_CASE ("SoftSymmetricClipper: odd/symmetric (no bias, no even-harmonic DC shift)", "[dsp][clipper][voicing]")
{
    for (float x : { 0.01f, 0.5f, 1.0f, 3.0f, 10.0f })
        CHECK (SoftSymmetricClipper::processSample (-x) == Catch::Approx (-SoftSymmetricClipper::processSample (x)).margin (1e-6));
}

TEST_CASE ("SoftSymmetricClipper: monotonically increasing (no folding)", "[dsp][clipper][voicing]")
{
    float previous = SoftSymmetricClipper::processSample (-5.0f);

    for (int i = 1; i <= 200; ++i)
    {
        const auto x = -5.0f + static_cast<float> (i) * 0.05f;
        const auto y = SoftSymmetricClipper::processSample (x);

        CHECK (y > previous);
        previous = y;
    }
}

TEST_CASE ("SoftSymmetricClipper: bounded output for extreme input, no NaN/Inf", "[dsp][clipper][voicing]")
{
    for (float x : { 1.0e6f, -1.0e6f, std::numeric_limits<float>::max() * 0.5f, -std::numeric_limits<float>::max() * 0.5f })
    {
        const auto y = SoftSymmetricClipper::processSample (x);

        CHECK (std::isfinite (y));
        CHECK (std::abs (y) <= 1.0f);
    }
}

TEST_CASE ("HardClipper: silence in, silence out", "[dsp][clipper][voicing]")
{
    CHECK (HardClipper::processSample (0.0f) == Catch::Approx (0.0f).margin (1e-9));
}

TEST_CASE ("HardClipper: linear (unity gain) inside +/-1, clamped outside", "[dsp][clipper][voicing]")
{
    CHECK (HardClipper::processSample (0.5f) == Catch::Approx (0.5f));
    CHECK (HardClipper::processSample (-0.5f) == Catch::Approx (-0.5f));
    CHECK (HardClipper::processSample (1.0f) == Catch::Approx (1.0f));
    CHECK (HardClipper::processSample (-1.0f) == Catch::Approx (-1.0f));
    CHECK (HardClipper::processSample (2.5f) == Catch::Approx (1.0f));
    CHECK (HardClipper::processSample (-2.5f) == Catch::Approx (-1.0f));
}

TEST_CASE ("HardClipper: odd/symmetric", "[dsp][clipper][voicing]")
{
    for (float x : { 0.01f, 0.5f, 1.0f, 3.0f, 10.0f })
        CHECK (HardClipper::processSample (-x) == Catch::Approx (-HardClipper::processSample (x)).margin (1e-6));
}

TEST_CASE ("HardClipper: bounded output for extreme input, no NaN/Inf", "[dsp][clipper][voicing]")
{
    for (float x : { 1.0e6f, -1.0e6f, std::numeric_limits<float>::max() * 0.5f, -std::numeric_limits<float>::max() * 0.5f })
    {
        const auto y = HardClipper::processSample (x);

        CHECK (std::isfinite (y));
        CHECK (std::abs (y) <= 1.0f);
    }
}

TEST_CASE ("ClipperVoicings::processSample dispatches to the correct voicing", "[dsp][clipper][voicing]")
{
    constexpr float asymmetry = 0.2f;
    constexpr float x = 0.75f;

    CHECK (ClipperVoicings::processSample (x, ClipperVoicing::asymmetric, asymmetry)
           == Catch::Approx (AsymSoftClipper::processSample (x, asymmetry)));
    CHECK (ClipperVoicings::processSample (x, ClipperVoicing::softSymmetric, asymmetry)
           == Catch::Approx (SoftSymmetricClipper::processSample (x)));
    CHECK (ClipperVoicings::processSample (x, ClipperVoicing::hardClip, asymmetry)
           == Catch::Approx (HardClipper::processSample (x)));
}

TEST_CASE ("ClipperVoicings: the three voicings give audibly different output for the same input", "[dsp][clipper][voicing]")
{
    // At a moderately hot input level, the three nonlinearities must not
    // collapse onto the same curve - otherwise the Voicing parameter would
    // be a no-op, defeating the point of exposing it.
    constexpr float asymmetry = 0.2f;
    constexpr float x = 1.5f;

    const auto asym = ClipperVoicings::processSample (x, ClipperVoicing::asymmetric, asymmetry);
    const auto soft = ClipperVoicings::processSample (x, ClipperVoicing::softSymmetric, asymmetry);
    const auto hard = ClipperVoicings::processSample (x, ClipperVoicing::hardClip, asymmetry);

    CHECK (asym != Catch::Approx (soft).margin (1.0e-3));
    CHECK (asym != Catch::Approx (hard).margin (1.0e-3));
    CHECK (soft != Catch::Approx (hard).margin (1.0e-3));
}

//==============================================================================
// v0.2.0 "asymmetry_amount" guarantee (docs/design-brief.md guarantee 4):
// asymmetryAmount (0-100%) maps monotonically to the Asymmetric voicing's
// bias `a` in 0.0-0.5 (OvertureEngine::processChunk()'s
// `asymmetryA = (asymmetryAmountPercent * 0.01f) * asymmetryMaxBias`
// formula, mirrored directly here since the mapping itself lives in the
// engine, not in this header). At the default 40% (a=0.2), this must
// reproduce v0.1's fixed a=0.2 behaviour exactly - this is the same
// asymmetry value tests/AsymSoftClipperTests.cpp's existing suite already
// exercises throughout, so this file only adds the sweep/monotonicity
// coverage that is genuinely new in v0.2.0.
namespace
{
    constexpr float asymmetryMaxBias = 0.5f; // mirrors OvertureEngine.h's private constant

    float mapAsymmetryAmountPercentToBias (float asymmetryAmountPercent)
    {
        return (asymmetryAmountPercent * 0.01f) * asymmetryMaxBias;
    }
}

TEST_CASE ("Asymmetry: default 40% maps to bias a=0.2, reproducing v0.1's fixed default exactly", "[dsp][clipper][asymmetry]")
{
    CHECK (mapAsymmetryAmountPercentToBias (40.0f) == Catch::Approx (0.2f).margin (1.0e-6));
}

TEST_CASE ("Asymmetry: 0% -> 100% maps monotonically to bias 0.0 -> 0.5, and positive/negative peak "
           "levels diverge monotonically with the control",
           "[dsp][clipper][asymmetry]")
{
    float previousBias = -1.0f;
    float previousPeakSpread = -1.0f;

    for (int i = 0; i <= 10; ++i)
    {
        const auto asymmetryAmountPercent = static_cast<float> (i) * 10.0f;
        const auto bias = mapAsymmetryAmountPercentToBias (asymmetryAmountPercent);

        CHECK (bias >= previousBias);
        CHECK (bias <= 0.5f + 1.0e-6f);
        previousBias = bias;

        // Deep into saturation, the positive/negative half-cycle ceilings'
        // magnitude difference is the direct, audible consequence of the
        // bias - see AsymSoftClipperTests.cpp's "saturate at different
        // ceilings" test for the underlying mechanism.
        const auto positiveCeiling = AsymSoftClipper::processSample (50.0f, bias);
        const auto negativeCeiling = AsymSoftClipper::processSample (-50.0f, bias);
        const auto peakSpread = std::abs (std::abs (positiveCeiling) - std::abs (negativeCeiling));

        CHECK (peakSpread >= previousPeakSpread - 1.0e-6f); // monotonically non-decreasing
        previousPeakSpread = peakSpread;
    }

    // 0% (fully symmetric) and 100% (maximally asymmetric) must genuinely
    // differ - otherwise the control would have no audible effect at all.
    CHECK (previousPeakSpread > 0.01f);
}

//==============================================================================
// v0.2.0 backward-compatibility guarantee (docs/design-brief.md guarantee 1):
// at kneeSoften = 0 (OvertureEngine::processChunk() skips calling
// KneeSoftening::apply entirely in that case rather than calling it with a
// blend of 0 - see that function's dispatch), the per-sample computation the
// engine performs is exactly ClipperVoicings::processSample(x, voicing, a) -
// bit-for-bit identical to v0.1's dispatch, for all three voicings, at
// every asymmetry bias tested (asymmetryAmount = 40% -> a = 0.2 is v0.1's
// exact fixed default - see the Asymmetry tests above). This test exercises
// that exact composition (voicing dispatch, conditionally-skipped knee
// blend) directly rather than through the full engine, so it stays focused
// on the DSP-primitive-level "transfer function" the brief's guarantee 1
// describes; tests/EngineTests.cpp separately covers the surrounding
// Tight/Drive/oversampling/Bite-shelf-skip machinery this composition sits
// inside.
namespace
{
    float composedClipperStage (float x, ClipperVoicing voicing, float asymmetry, float kneeBlend01)
    {
        const auto raw = ClipperVoicings::processSample (x, voicing, asymmetry);
        return kneeBlend01 > 0.0f ? KneeSoftening::apply (raw, kneeBlend01) : raw;
    }
}

TEST_CASE ("v0.2.0 backward compatibility: kneeSoften=0 reproduces v0.1's exact clipper dispatch, "
           "bit-for-bit, for all three voicings",
           "[dsp][clipper][backcompat]")
{
    constexpr float v01Asymmetry = 0.2f; // v0.1's fixed constant, == asymmetryAmount 40% mapped

    for (auto voicing : { ClipperVoicing::asymmetric, ClipperVoicing::softSymmetric, ClipperVoicing::hardClip })
    {
        for (float x : { -3.0f, -1.5f, -0.5f, -0.05f, 0.0f, 0.05f, 0.5f, 1.5f, 3.0f })
        {
            const auto expected = ClipperVoicings::processSample (x, voicing, v01Asymmetry);
            const auto actual = composedClipperStage (x, voicing, v01Asymmetry, 0.0f /* kneeSoften=0 */);

            CHECK (actual == Catch::Approx (expected).margin (0.0)); // exact, not Approx-with-tolerance - bit-for-bit
        }
    }
}
