#include "dsp/NoiseGate.h"
#include "dsp/OvertureEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// Measurable acceptance tests for Overture v0.3.0's built-in noise gate
// (src/dsp/NoiseGate.h), T-G1..T-G7 of the v0.3.0 brief SS6.
//
// Every assertion below is a measurement of the gate's actual control
// signal, not a smoke test: the opening ramp is timed between the 10% and
// 90% points of the linear gain, the closing ramp is LINE-FITTED in dB (an
// exponential release would fail the R^2 check even if it hit the same
// endpoints), the hold is measured between the detector crossing the closing
// threshold and the gain leaving unity, and the anti-chatter claims are
// counted as state transitions rather than eyeballed.
//
// Thresholds are CALIBRATED rather than assumed: the detector is an
// exponential mean-square follower behind a 100 Hz/5 kHz sidechain pair, so
// the dB it reports for a given amplitude depends on both the crest factor
// and the test frequency. Every test therefore measures the settled detector
// level for its own stimulus first and places the threshold relative to
// that. Hard-coding "amplitude 0.0178 == -38 dB" would be testing arithmetic
// the gate never performs.
namespace
{
    using basilica::dsp::NoiseGate;

    constexpr double gateSampleRate = 48000.0;

    struct GateRun
    {
        std::vector<double> gainDb;
        std::vector<double> detectorDb;
        std::vector<float> gain;
        int stateTransitions = 0;
    };

    // Runs the gate over `numSamples` of a caller-supplied signal and
    // records the whole control path.
    GateRun runGate (NoiseGate& gate, int numSamples, const std::function<float (int)>& signal)
    {
        GateRun run;
        run.gainDb.reserve (static_cast<size_t> (numSamples));
        run.detectorDb.reserve (static_cast<size_t> (numSamples));
        run.gain.reserve (static_cast<size_t> (numSamples));

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto g = gate.processSample (std::abs (signal (sample)));
            run.gain.push_back (g);
            run.gainDb.push_back (gate.getGainDb());
            run.detectorDb.push_back (gate.getDetectorDb());
        }

        run.stateTransitions = gate.getStateTransitionCount();
        return run;
    }

    std::function<float (int)> sine (double frequencyHz, double amplitude, double sampleRate = gateSampleRate)
    {
        return [frequencyHz, amplitude, sampleRate] (int sample)
        {
            return static_cast<float> (amplitude
                                        * std::sin (juce::MathConstants<double>::twoPi * frequencyHz
                                                    * static_cast<double> (sample) / sampleRate));
        };
    }

    // Settled detector reading (dB) for a stimulus, measured with the gate
    // pinned wide open so nothing the state machine does can perturb it.
    double measureDetectorDb (const std::function<float (int)>& signal, double seconds = 0.4)
    {
        NoiseGate gate;
        gate.prepare (gateSampleRate, 1);
        gate.setThresholdDb (-200.0);
        gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

        const auto numSamples = static_cast<int> (seconds * gateSampleRate);
        const auto run = runGate (gate, numSamples, signal);
        return run.detectorDb.back();
    }

    // Least-squares line fit of y against x, returning slope and R^2.
    struct LineFit
    {
        double slope = 0.0;
        double intercept = 0.0;
        double rSquared = 0.0;
    };

    LineFit fitLine (const std::vector<double>& x, const std::vector<double>& y)
    {
        REQUIRE (x.size() == y.size());
        REQUIRE (x.size() >= 3);

        const auto n = static_cast<double> (x.size());
        double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            sumX += x[i];
            sumY += y[i];
            sumXX += x[i] * x[i];
            sumXY += x[i] * y[i];
        }

        LineFit fit;
        const auto denominator = n * sumXX - sumX * sumX;
        fit.slope = (n * sumXY - sumX * sumY) / denominator;
        fit.intercept = (sumY - fit.slope * sumX) / n;

        const auto meanY = sumY / n;
        double residual = 0.0, total = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto predicted = fit.intercept + fit.slope * x[i];
            residual += (y[i] - predicted) * (y[i] - predicted);
            total += (y[i] - meanY) * (y[i] - meanY);
        }

        fit.rSquared = total > 0.0 ? 1.0 - residual / total : 0.0;
        return fit;
    }
}

//==============================================================================
// T-G1
TEST_CASE ("T-G1: static levels - the gate is fully open above threshold and at the floor below it",
           "[gate][v030]")
{
    const auto toneAmplitude = 0.05;
    const auto detectorDb = measureDetectorDb (sine (1000.0, toneAmplitude));

    SECTION ("12 dB above threshold: exactly unity gain")
    {
        NoiseGate gate;
        gate.prepare (gateSampleRate, 1);
        gate.setThresholdDb (detectorDb - 12.0);
        gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

        const auto run = runGate (gate, static_cast<int> (0.5 * gateSampleRate), sine (1000.0, toneAmplitude));

        CHECK (run.gainDb.back() == Catch::Approx (0.0).margin (0.1));
        // The opening branch converges to an exact 1.0f, so a fully open
        // gate is a true bypass rather than a near-unity multiply.
        CHECK (run.gain.back() == 1.0f);
    }

    SECTION ("20 dB below threshold: the gate reaches its range floor")
    {
        NoiseGate gate;
        gate.prepare (gateSampleRate, 1);
        gate.setThresholdDb (detectorDb + 20.0);
        gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

        const auto run = runGate (gate, static_cast<int> (1.0 * gateSampleRate), sine (1000.0, toneAmplitude));

        INFO ("settled gain " << run.gainDb.back() << " dB");
        CHECK (run.gainDb.back() <= -80.0);
        CHECK (run.gainDb.back() >= -NoiseGate::rangeDb - 1.0e-9); // never below the declared floor
    }
}

//==============================================================================
// T-G2
TEST_CASE ("T-G2: opening ramp - 10%..90% of linear gain inside 0.05..0.5 ms", "[gate][v030]")
{
    // A burst from -80 dBFS to -6 dBFS. The threshold sits well below the
    // burst so the state machine flips as soon as the detector notices it,
    // isolating the ramp itself from the detector's settling.
    const auto quietAmplitude = 1.0e-4;
    const auto loudAmplitude = 0.5;

    const auto loudDetectorDb = measureDetectorDb (sine (1000.0, loudAmplitude));

    NoiseGate gate;
    gate.prepare (gateSampleRate, 1);
    gate.setThresholdDb (loudDetectorDb - 30.0);
    gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

    const auto burstStart = static_cast<int> (0.4 * gateSampleRate); // long enough to reach the floor first
    const auto numSamples = burstStart + static_cast<int> (0.05 * gateSampleRate);

    const auto run = runGate (gate, numSamples, [&] (int sample)
    {
        const auto amplitude = sample < burstStart ? quietAmplitude : loudAmplitude;
        return static_cast<float> (amplitude
                                    * std::sin (juce::MathConstants<double>::twoPi * 1000.0
                                                * static_cast<double> (sample) / gateSampleRate));
    });

    // The gate must genuinely have been shut before the burst, or there is
    // no ramp to measure.
    REQUIRE (run.gainDb[static_cast<size_t> (burstStart) - 1] <= -80.0);

    const auto findFirstAtOrAbove = [&] (float target, int from)
    {
        for (int sample = from; sample < numSamples; ++sample)
            if (run.gain[static_cast<size_t> (sample)] >= target)
                return sample;

        return -1;
    };

    const auto tenPercent = findFirstAtOrAbove (0.1f, burstStart);
    const auto ninetyPercent = findFirstAtOrAbove (0.9f, burstStart);

    REQUIRE (tenPercent > 0);
    REQUIRE (ninetyPercent > tenPercent);

    const auto riseMs = 1000.0 * static_cast<double> (ninetyPercent - tenPercent) / gateSampleRate;
    INFO ("10-90% rise " << riseMs << " ms (tau_att = " << (NoiseGate::attackTauSeconds * 1000.0) << " ms)");

    CHECK (riseMs >= 0.05);
    CHECK (riseMs <= 0.5);
}

//==============================================================================
// T-G3
TEST_CASE ("T-G3: retriggering hold measures 20 ms +/- 10% from the detector crossing the closing threshold",
           "[gate][v030]")
{
    // The hold is a property of the STATE MACHINE, not of the detector: the
    // detector's own fall time from programme level to the closing threshold
    // is a separate (and much larger) quantity that depends on where the
    // threshold sits. Measuring between the detector crossing T_close and
    // the gain leaving unity is what isolates the 20 ms the gate actually
    // promises - which is exactly why NoiseGate exposes getDetectorDb().
    const auto loudAmplitude = 0.5;
    const auto loudDetectorDb = measureDetectorDb (sine (1000.0, loudAmplitude));

    NoiseGate gate;
    gate.prepare (gateSampleRate, 1);
    gate.setThresholdDb (loudDetectorDb - 20.0);
    gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

    const auto burstEnd = static_cast<int> (0.3 * gateSampleRate);
    const auto numSamples = burstEnd + static_cast<int> (0.4 * gateSampleRate);

    const auto run = runGate (gate, numSamples, [&] (int sample)
    {
        if (sample >= burstEnd)
            return 0.0f;

        return static_cast<float> (loudAmplitude
                                    * std::sin (juce::MathConstants<double>::twoPi * 1000.0
                                                * static_cast<double> (sample) / gateSampleRate));
    });

    REQUIRE (run.gainDb[static_cast<size_t> (burstEnd) - 1] == Catch::Approx (0.0).margin (0.05));

    const auto closeThresholdDb = gate.getSmoothedThresholdDb() - NoiseGate::hysteresisDb;

    int crossing = -1;
    for (int sample = burstEnd; sample < numSamples; ++sample)
        if (run.detectorDb[static_cast<size_t> (sample)] <= closeThresholdDb)
        {
            crossing = sample;
            break;
        }

    REQUIRE (crossing > 0);

    int fell = -1;
    for (int sample = crossing; sample < numSamples; ++sample)
        if (run.gainDb[static_cast<size_t> (sample)] <= -1.0)
        {
            fell = sample;
            break;
        }

    REQUIRE (fell > crossing);

    const auto holdMs = 1000.0 * static_cast<double> (fell - crossing) / gateSampleRate;
    INFO ("measured hold " << holdMs << " ms");

    CHECK (holdMs >= 0.9 * NoiseGate::holdSeconds * 1000.0);
    CHECK (holdMs <= 1.1 * NoiseGate::holdSeconds * 1000.0);
}

//==============================================================================
// T-G4
TEST_CASE ("T-G4: the closing ramp is dB-LINEAR at the declared slope, not exponential", "[gate][v030]")
{
    // This is the DN100/VCA signature the whole gate is built around, and it
    // is the one property a naive exponential-release gate cannot fake: fit
    // a straight line to 20*log10(gain) and demand both the right slope and
    // an R^2 that an exponential-in-linear-gain ramp could never reach.
    const auto loudAmplitude = 0.5;
    const auto loudDetectorDb = measureDetectorDb (sine (1000.0, loudAmplitude));

    NoiseGate gate;
    gate.prepare (gateSampleRate, 1);
    gate.setThresholdDb (loudDetectorDb - 20.0);
    gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

    const auto burstEnd = static_cast<int> (0.3 * gateSampleRate);
    const auto numSamples = burstEnd + static_cast<int> (0.5 * gateSampleRate);

    const auto run = runGate (gate, numSamples, [&] (int sample)
    {
        if (sample >= burstEnd)
            return 0.0f;

        return static_cast<float> (loudAmplitude
                                    * std::sin (juce::MathConstants<double>::twoPi * 1000.0
                                                * static_cast<double> (sample) / gateSampleRate));
    });

    // Fit over the strictly-interior part of the ramp (-10 dB to -70 dB), so
    // neither the hold plateau nor the floor clamp biases the slope.
    std::vector<double> times;
    std::vector<double> levels;

    for (int sample = burstEnd; sample < numSamples; ++sample)
    {
        const auto value = run.gainDb[static_cast<size_t> (sample)];

        if (value <= -10.0 && value >= -70.0)
        {
            times.push_back (static_cast<double> (sample) / gateSampleRate);
            levels.push_back (value);
        }
    }

    REQUIRE (times.size() > 100);

    const auto fit = fitLine (times, levels);
    INFO ("fitted slope " << fit.slope << " dB/s, R^2 " << fit.rSquared);

    CHECK (std::abs (fit.slope) == Catch::Approx (NoiseGate::fastSlopeDbPerSecond).epsilon (0.10));
    CHECK (fit.rSquared > 0.99);
}

//==============================================================================
// T-G5
TEST_CASE ("T-G5: hysteresis and the NLC smoother stop the gate chattering", "[gate][v030]")
{
    SECTION ("amplitude dithered +/- 1.5 dB around the threshold: no chatter after the first open")
    {
        const auto baseAmplitude = 0.05;
        const auto detectorDb = measureDetectorDb (sine (1000.0, baseAmplitude));

        NoiseGate gate;
        gate.prepare (gateSampleRate, 1);
        gate.setThresholdDb (detectorDb);
        gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

        const auto numSamples = static_cast<int> (2.0 * gateSampleRate);

        // 7 Hz amplitude dither of +/- 1.5 dB - fast enough to chatter a
        // hysteresis-free gate many times per second.
        const auto run = runGate (gate, numSamples, [&] (int sample)
        {
            const auto t = static_cast<double> (sample) / gateSampleRate;
            const auto ditherDb = 1.5 * std::sin (juce::MathConstants<double>::twoPi * 7.0 * t);
            const auto amplitude = baseAmplitude * std::pow (10.0, ditherDb / 20.0);
            return static_cast<float> (amplitude * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t));
        });

        INFO ("state transitions over 2 s: " << run.stateTransitions);
        // The dither never reaches T - H, so after the initial settle the
        // state machine must not move at all.
        CHECK (run.stateTransitions <= 1);
    }

    SECTION ("70 Hz sine exactly at the threshold: <= 1 transition per second")
    {
        // The RMS-ripple worst case: at tau_rms = 5 ms a 70 Hz tone leaves
        // roughly +/- 1 dB of ripple on the raw detector. The NLC smoother's
        // slow branch (tau 30 ms for sub-knee steps) is what has to remove
        // it - a plain fast detector would chatter at 70 Hz.
        const auto amplitude = 0.05;
        const auto detectorDb = measureDetectorDb (sine (70.0, amplitude), 1.0);

        NoiseGate gate;
        gate.prepare (gateSampleRate, 1);
        gate.setThresholdDb (detectorDb);
        gate.setReleaseMode (NoiseGate::ReleaseMode::fast);

        const auto seconds = 2.0;
        const auto numSamples = static_cast<int> (seconds * gateSampleRate);
        const auto run = runGate (gate, numSamples, sine (70.0, amplitude));

        const auto transitionsPerSecond = static_cast<double> (run.stateTransitions) / seconds;
        INFO ("transitions/s: " << transitionsPerSecond);
        CHECK (transitionsPerSecond <= 1.0);
    }
}

//==============================================================================
// T-G6
TEST_CASE ("T-G6: the Auto release tells a staccato mute apart from a decaying note", "[gate][v030]")
{
    SECTION ("(a) staccato stop: the gate reaches -80 dB within 150 ms")
    {
        const auto loudAmplitude = 0.5;
        const auto loudDetectorDb = measureDetectorDb (sine (1000.0, loudAmplitude));

        NoiseGate gate;
        gate.prepare (gateSampleRate, 1);
        gate.setThresholdDb (loudDetectorDb - 20.0);
        gate.setReleaseMode (NoiseGate::ReleaseMode::automatic);

        const auto stopAt = static_cast<int> (0.4 * gateSampleRate);
        const auto numSamples = stopAt + static_cast<int> (0.3 * gateSampleRate);

        const auto run = runGate (gate, numSamples, [&] (int sample)
        {
            if (sample >= stopAt)
                return 0.0f;

            return static_cast<float> (loudAmplitude
                                        * std::sin (juce::MathConstants<double>::twoPi * 1000.0
                                                    * static_cast<double> (sample) / gateSampleRate));
        });

        int reached = -1;
        for (int sample = stopAt; sample < numSamples; ++sample)
            if (run.gainDb[static_cast<size_t> (sample)] <= -80.0)
            {
                reached = sample;
                break;
            }

        REQUIRE (reached > 0);

        const auto ms = 1000.0 * static_cast<double> (reached - stopAt) / gateSampleRate;
        INFO ("time to -80 dB after the stop: " << ms << " ms");
        CHECK (ms <= 150.0);

        // ...and it got there because the TVP discriminator dumped to the
        // fast slope, not because the fixed Slow slope happens to be quick.
        CHECK (gate.getReleaseSlopeDbPerSecond() == Catch::Approx (NoiseGate::tvpFastSlopeDbPerSecond));
    }

    SECTION ("(b) a 30 dB/s decaying note is never truncated")
    {
        const auto startAmplitude = 0.5;
        const auto startDetectorDb = measureDetectorDb (sine (1000.0, startAmplitude));

        NoiseGate gate;
        gate.prepare (gateSampleRate, 1);
        gate.setThresholdDb (startDetectorDb - 30.0);
        gate.setReleaseMode (NoiseGate::ReleaseMode::automatic);

        // 30 dB/s decay: the note is still 4 dB above the closing threshold
        // after 0.87 s, so for the whole measured second the gate has no
        // business closing at all.
        const auto seconds = 0.85;
        const auto numSamples = static_cast<int> (seconds * gateSampleRate);

        const auto run = runGate (gate, numSamples, [&] (int sample)
        {
            const auto t = static_cast<double> (sample) / gateSampleRate;
            const auto amplitude = startAmplitude * std::pow (10.0, -30.0 * t / 20.0);
            return static_cast<float> (amplitude * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t));
        });

        double worstDb = 0.0;
        for (int sample = static_cast<int> (0.05 * gateSampleRate); sample < numSamples; ++sample)
            worstDb = std::min (worstDb, run.gainDb[static_cast<size_t> (sample)]);

        INFO ("worst gain during the decay: " << worstDb << " dB");
        CHECK (worstDb >= -1.5);

        // The discriminator must be in its SLOW/tracking branch, i.e. it
        // recognised a decay rather than a stop.
        CHECK (gate.getReleaseSlopeDbPerSecond() < NoiseGate::tvpFastSlopeDbPerSecond);
    }
}

//==============================================================================
// T-G7
TEST_CASE ("T-G7: a fully open gate is a bit-exact bypass of the wet path", "[gate][v030][engine]")
{
    // Engine level, because "the gate is transparent when open" is only
    // interesting if it survives the integration - the gain is applied to
    // the wet path input, before the Tight HPF, so a bug there would show up
    // as a filtered or delayed signal rather than as an obviously wrong one.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 24;

    const auto run = [&] (bool gateEnabled)
    {
        OvertureEngine engine;
        engine.setTightFrequencyHz (100.0f);
        engine.setDriveDb (10.0f);
        engine.setBiteAmountPercent (65.0f);
        engine.setKneeSoftenPercent (40.0f);
        engine.setAsymmetryAmountPercent (40.0f);
        engine.setBiteTiltPercent (0.0f);
        engine.setLevelDb (0.0f);
        engine.setMixProportion (1.0f);

        juce::dsp::ProcessSpec spec { sampleRate, blockSize, 2 };
        engine.prepare (spec);

        engine.setGateEnabled (gateEnabled);
        engine.setGateThresholdDb (-80.0f); // programme sits ~30 dB above this
        engine.setGateReleaseMode (basilica::dsp::NoiseGate::ReleaseMode::automatic);

        std::vector<float> output;
        juce::AudioBuffer<float> buffer (2, blockSize);

        for (int block = 0; block < numBlocks; ++block)
        {
            TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.35f, static_cast<juce::int64> (block) * blockSize);

            juce::dsp::AudioBlock<float> audioBlock (buffer);
            engine.process (audioBlock);

            const auto* data = buffer.getReadPointer (0);
            output.insert (output.end(), data, data + blockSize);
        }

        return output;
    };

    const auto withGate = run (true);
    const auto withoutGate = run (false);

    REQUIRE (withGate.size() == withoutGate.size());

    double sumOfSquares = 0.0;
    double worst = 0.0;

    for (size_t i = 0; i < withGate.size(); ++i)
    {
        const auto residual = static_cast<double> (withGate[i]) - static_cast<double> (withoutGate[i]);
        sumOfSquares += residual * residual;
        worst = std::max (worst, std::abs (residual));
    }

    const auto residualDb = 10.0 * std::log10 (sumOfSquares / static_cast<double> (withGate.size()) + 1.0e-30);
    INFO ("residual " << residualDb << " dBFS RMS, worst sample " << worst);

    CHECK (residualDb < -120.0);
}

//==============================================================================
// Integration: engaging the gate mid-signal must not click or mute.
TEST_CASE ("Engaging the gate on a sustained note neither clicks nor mutes", "[gate][v030][engine]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    OvertureEngine engine;
    engine.setTightFrequencyHz (100.0f);
    engine.setDriveDb (0.0f);
    engine.setBiteAmountPercent (0.0f);
    engine.setKneeSoftenPercent (0.0f);
    engine.setBiteTiltPercent (0.0f);
    engine.setLevelDb (0.0f);
    engine.setMixProportion (1.0f);
    engine.setClipperVoicing (ClipperVoicing::softSymmetric);

    juce::dsp::ProcessSpec spec { sampleRate, blockSize, 1 };
    engine.prepare (spec);

    engine.setGateThresholdDb (-60.0f);
    engine.setGateReleaseMode (basilica::dsp::NoiseGate::ReleaseMode::automatic);

    juce::AudioBuffer<float> buffer (1, blockSize);
    std::vector<float> output;

    for (int block = 0; block < 20; ++block)
    {
        // Engage the gate part-way through, mid-note.
        engine.setGateEnabled (block >= 8);

        TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.3f, static_cast<juce::int64> (block) * blockSize);

        juce::dsp::AudioBlock<float> audioBlock (buffer);
        engine.process (audioBlock);

        const auto* data = buffer.getReadPointer (0);
        output.insert (output.end(), data, data + blockSize);
    }

    // No sample-to-sample discontinuity anywhere near the switch: a 220 Hz
    // sine at 0.3 moves at most ~0.0086 per sample, so anything above 0.05 is
    // a click rather than programme material.
    double worstStep = 0.0;
    const auto switchSample = static_cast<size_t> (8 * blockSize);

    for (size_t i = switchSample - 64; i < switchSample + static_cast<size_t> (4 * blockSize); ++i)
        worstStep = std::max (worstStep, std::abs (static_cast<double> (output[i]) - static_cast<double> (output[i - 1])));

    INFO ("worst sample-to-sample step around the switch: " << worstStep);
    CHECK (worstStep < 0.05);

    // ...and the note is still there afterwards (no multi-millisecond mute
    // while the detector climbs out of digital silence).
    double afterRms = 0.0;
    for (size_t i = switchSample; i < switchSample + static_cast<size_t> (blockSize); ++i)
        afterRms += static_cast<double> (output[i]) * static_cast<double> (output[i]);

    afterRms = std::sqrt (afterRms / static_cast<double> (blockSize));
    CHECK (afterRms > 0.15);
}
