#include "dsp/AsymSoftClipper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

TEST_CASE ("AsymSoftClipper: silence in, silence out, for any asymmetry", "[dsp][clipper]")
{
    CHECK (AsymSoftClipper::processSample (0.0f, 0.0f) == Catch::Approx (0.0f).margin (1e-9));
    CHECK (AsymSoftClipper::processSample (0.0f, 0.2f) == Catch::Approx (0.0f).margin (1e-9));
    CHECK (AsymSoftClipper::processSample (0.0f, -0.35f) == Catch::Approx (0.0f).margin (1e-9));
}

TEST_CASE ("AsymSoftClipper: near-linear for small-signal input", "[dsp][clipper]")
{
    // For small x, tanh(x + a) - tanh(a) ~= x * sech^2(a) (first-order Taylor
    // expansion around x=0). Checking against that predicted small-signal
    // gain - rather than against raw unity gain - is the correct
    // "near-linear" check for a curve that has a fixed insertion loss/gain
    // by design (the asymmetry bias itself changes the slope at the
    // operating point, independent of any drive/gain staging elsewhere in
    // the chain).
    constexpr float asymmetry = 0.2f;
    const auto sech2 = 1.0f - std::tanh (asymmetry) * std::tanh (asymmetry);

    for (float x : { 1.0e-4f, 5.0e-4f, 1.0e-3f, -1.0e-3f, 2.0e-3f })
    {
        const auto y = AsymSoftClipper::processSample (x, asymmetry);
        const auto predictedLinear = x * sech2;

        CHECK (y == Catch::Approx (predictedLinear).margin (1.0e-6));
    }
}

TEST_CASE ("AsymSoftClipper: monotonically increasing (no folding)", "[dsp][clipper]")
{
    constexpr float asymmetry = 0.2f;
    float previous = AsymSoftClipper::processSample (-5.0f, asymmetry);

    for (int i = 1; i <= 200; ++i)
    {
        const auto x = -5.0f + static_cast<float> (i) * 0.05f;
        const auto y = AsymSoftClipper::processSample (x, asymmetry);

        CHECK (y > previous);
        previous = y;
    }
}

TEST_CASE ("AsymSoftClipper: bounded output for extreme input, no NaN/Inf", "[dsp][clipper]")
{
    constexpr float asymmetry = 0.2f;

    for (float x : { 1.0e6f, -1.0e6f, std::numeric_limits<float>::max() * 0.5f,
                      -std::numeric_limits<float>::max() * 0.5f, 0.0f })
    {
        const auto y = AsymSoftClipper::processSample (x, asymmetry);

        CHECK (std::isfinite (y));
        CHECK (std::abs (y) < 2.5f); // tanh() saturates well inside +/-2.5 for any finite bias here
    }
}

TEST_CASE ("AsymSoftClipper: positive and negative half-cycles saturate at different ceilings", "[dsp][clipper]")
{
    constexpr float asymmetry = 0.2f;

    const auto positiveCeiling = AsymSoftClipper::processSample (50.0f, asymmetry);
    const auto negativeCeiling = AsymSoftClipper::processSample (-50.0f, asymmetry);

    // Genuinely asymmetric: the two ceilings must differ in magnitude.
    CHECK (std::abs (positiveCeiling) != Catch::Approx (std::abs (negativeCeiling)).margin (1.0e-3));

    // Zero asymmetry recovers a symmetric (odd) tanh curve as a sanity check
    // that the asymmetry term - not some other bug - is what causes the
    // difference above.
    const auto symmetricPositive = AsymSoftClipper::processSample (50.0f, 0.0f);
    const auto symmetricNegative = AsymSoftClipper::processSample (-50.0f, 0.0f);
    CHECK (symmetricPositive == Catch::Approx (-symmetricNegative).margin (1.0e-6));
}
