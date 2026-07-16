#include "dsp/KneeSoftening.h"
#include "dsp/ClipperVoicing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

TEST_CASE ("KneeSoftening::apply at blendAmount=0 is the identity function (v0.1 backward compatibility)", "[dsp][knee]")
{
    for (float raw : { -1.0f, -0.5f, -0.1f, 0.0f, 0.1f, 0.5f, 1.0f, 2.5f, -2.5f })
        CHECK (KneeSoftening::apply (raw, 0.0f) == Catch::Approx (raw).margin (0.0)); // exact, not Approx-with-tolerance - bit-for-bit
}

TEST_CASE ("KneeSoftening::apply at blendAmount=1 fully replaces raw with tanh(raw)", "[dsp][knee]")
{
    for (float raw : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.5f })
        CHECK (KneeSoftening::apply (raw, 1.0f) == Catch::Approx (std::tanh (raw)).margin (1.0e-6));
}

TEST_CASE ("KneeSoftening::apply interpolates monotonically between raw and tanh(raw) as blendAmount sweeps 0->1", "[dsp][knee]")
{
    constexpr float raw = 0.9f; // std::tanh(0.9) < 0.9, so softened < raw here
    const auto target = std::tanh (raw);

    float previousDistance = std::abs (raw - target);

    for (int i = 1; i <= 20; ++i)
    {
        const auto blend = static_cast<float> (i) / 20.0f;
        const auto y = KneeSoftening::apply (raw, blend);
        const auto distance = std::abs (y - target);

        CHECK (distance <= previousDistance + 1.0e-6f); // monotonically approaching tanh(raw)
        previousDistance = distance;
    }

    CHECK (KneeSoftening::apply (raw, 1.0f) == Catch::Approx (target).margin (1.0e-6));
}

TEST_CASE ("KneeSoftening::apply rounds Hard Clip's corner (v0.1 had literally zero knee)", "[dsp][knee][voicing]")
{
    // v0.1's Hard Clip has zero knee at any drive level (a razor-sharp clamp
    // to +/-1) - docs/design-brief.md's core motivation for exposing Knee
    // Soften even for this voicing. Confirms the softened variant actually
    // pulls the clipped ceiling down from exactly 1.0 (tanh(1.0) < 1.0),
    // proving Hard Clip is genuinely softenable, not a no-op for this
    // voicing.
    const auto hardClipped = HardClipper::processSample (5.0f); // deep into the clamp, == 1.0f exactly
    REQUIRE (hardClipped == Catch::Approx (1.0f));

    const auto softened = KneeSoftening::apply (hardClipped, 0.5f);
    CHECK (softened < hardClipped);
    CHECK (softened > 0.0f);
}

TEST_CASE ("KneeSoftening::apply bounded output for extreme input, no NaN/Inf", "[dsp][knee]")
{
    for (float raw : { 1.0e6f, -1.0e6f, std::numeric_limits<float>::max() * 0.5f, -std::numeric_limits<float>::max() * 0.5f })
    {
        for (float blend : { 0.0f, 0.5f, 1.0f })
        {
            const auto y = KneeSoftening::apply (raw, blend);
            CHECK (std::isfinite (y));
        }
    }
}
