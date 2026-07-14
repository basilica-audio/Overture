#include "dsp/ClipperVoicing.h"

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
