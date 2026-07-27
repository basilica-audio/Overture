#include "dsp/FeedbackClipperStage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

// Measurable acceptance tests for the circuit-solved Feedback voicing
// (src/dsp/FeedbackClipperStage.h), T-F1..T-F5 of the v0.3.0 brief SS6.
//
// The point of these tests is that the stage is a CIRCUIT SOLVER, so it can
// be checked against the circuit rather than against itself:
//
//   - its linear regime has a closed-form digital transfer function, derived
//     from the same bilinear/trapezoidal discretisation the solver uses, so
//     the measured small-signal response can be compared to an absolute
//     prediction (T-F5) instead of to a recorded snapshot;
//   - its harmonic structure at programme level can be compared against the
//     SAME solver run at 32x the sample rate (T-F1/T-F2), which is the
//     numerical ground truth for a trapezoidal ODE solve;
//   - its Newton iteration count and finiteness can be swept over a hostile
//     grid (T-F4).
//
// Calibration note (binding, brief SS3.2): V_SCALE = 2.0 V per full scale
// and R1/R2 give ~21.5 dB of minimum in-band small-signal gain into a
// ~0.4 V diode knee, so this voicing's linear region at Drive 0 ends around
// -40...-35 dBFS. It is a touch-sensitive PROGRAMME-LEVEL clipper by design;
// a -12 dBFS input clips it hard, and T-F1 asserts exactly that. A "clean at
// -12 dBFS" result would mean a broken solver or a mis-scaled V_SCALE and
// must fail.
namespace
{
    using basilica::dsp::FeedbackClipperStage;

    // Exact complex DFT at an integer bin - no window needed, and no
    // spectral leakage, because every stimulus below is generated
    // bin-centred.
    std::complex<double> dftAtBin (const std::vector<double>& signal, int bin)
    {
        const auto n = static_cast<double> (signal.size());
        std::complex<double> sum { 0.0, 0.0 };

        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto phase = -juce::MathConstants<double>::twoPi * static_cast<double> (bin)
                               * static_cast<double> (i) / n;
            sum += signal[i] * std::complex<double> (std::cos (phase), std::sin (phase));
        }

        return sum * (2.0 / n);
    }

    struct HarmonicMeasurement
    {
        double fundamental = 0.0;
        std::vector<double> harmonics; // index 0 == H2
        double thdPercent = 0.0;
    };

    // Runs the stage on a bin-centred sine and returns H1..H10.
    HarmonicMeasurement measureHarmonics (double sampleRate,
                                          int fundamentalBin,
                                          int fftSize,
                                          double amplitudeVolts,
                                          double driveDb,
                                          double asymmetry01,
                                          int highestHarmonic = 10)
    {
        FeedbackClipperStage stage;
        stage.prepare (sampleRate, 1);
        stage.setDriveDb (driveDb);
        stage.setAsymmetry01 (asymmetry01);
        stage.snapSmoothingToTarget();

        const auto amplitudeFullScale = amplitudeVolts / FeedbackClipperStage::voltageScale;

        // Settle: several cycles of the 720 Hz pre-emphasis time constant
        // plus the resistance smoother, discarded.
        const auto settleSamples = static_cast<int> (0.2 * sampleRate);

        for (int i = 0; i < settleSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * static_cast<double> (fundamentalBin)
                               * static_cast<double> (i) / static_cast<double> (fftSize);
            stage.processSample (0, static_cast<float> (amplitudeFullScale * std::sin (phase)));
        }

        std::vector<double> output (static_cast<size_t> (fftSize));

        for (int i = 0; i < fftSize; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * static_cast<double> (fundamentalBin)
                               * static_cast<double> (settleSamples + i) / static_cast<double> (fftSize);
            output[static_cast<size_t> (i)] =
                static_cast<double> (stage.processSample (0, static_cast<float> (amplitudeFullScale * std::sin (phase))));
        }

        HarmonicMeasurement measurement;
        measurement.fundamental = std::abs (dftAtBin (output, fundamentalBin));

        double harmonicPower = 0.0;

        for (int h = 2; h <= highestHarmonic; ++h)
        {
            const auto bin = fundamentalBin * h;

            if (bin >= fftSize / 2)
            {
                measurement.harmonics.push_back (0.0);
                continue;
            }

            const auto magnitude = std::abs (dftAtBin (output, bin));
            measurement.harmonics.push_back (magnitude);
            harmonicPower += magnitude * magnitude;
        }

        measurement.thdPercent = measurement.fundamental > 0.0
                                     ? 100.0 * std::sqrt (harmonicPower) / measurement.fundamental
                                     : 0.0;
        return measurement;
    }

    double toDbc (double magnitude, double fundamental)
    {
        return 20.0 * std::log10 (magnitude / fundamental + 1.0e-30);
    }

    // Measured complex small-signal response at a bin-centred frequency.
    std::complex<double> measureResponse (double sampleRate,
                                          int bin,
                                          int fftSize,
                                          double amplitudeVolts,
                                          double driveDb)
    {
        FeedbackClipperStage stage;
        stage.prepare (sampleRate, 1);
        stage.setDriveDb (driveDb);
        stage.setAsymmetry01 (0.0);
        stage.snapSmoothingToTarget();

        const auto amplitudeFullScale = amplitudeVolts / FeedbackClipperStage::voltageScale;
        const auto settleSamples = fftSize;

        const auto sample = [&] (int index)
        {
            const auto phase = juce::MathConstants<double>::twoPi * static_cast<double> (bin)
                               * static_cast<double> (index) / static_cast<double> (fftSize);
            return static_cast<float> (amplitudeFullScale * std::sin (phase));
        };

        for (int i = 0; i < settleSamples; ++i)
            stage.processSample (0, sample (i));

        std::vector<double> output (static_cast<size_t> (fftSize));
        std::vector<double> input (static_cast<size_t> (fftSize));

        for (int i = 0; i < fftSize; ++i)
        {
            input[static_cast<size_t> (i)] = static_cast<double> (sample (settleSamples + i));
            output[static_cast<size_t> (i)] =
                static_cast<double> (stage.processSample (0, sample (settleSamples + i)));
        }

        return dftAtBin (output, bin) / dftAtBin (input, bin);
    }

    // Least-squares fit of the FEEDBACK term H(f) - 1 to the circuit's own
    // one-zero/one-pole small-signal shape
    //
    //     |K| * (f/fz)/sqrt(1 + (f/fz)^2) * 1/sqrt(1 + (f/fp)^2)
    //
    // A naive -3 dB corner search cannot be used here: at Drive max the
    // 720 Hz zero and the ~6 kHz pole are only three octaves apart, so
    // neither corner sits on a flat plateau and both readings are pulled
    // towards each other. Fitting the whole measured curve to the model the
    // circuit actually has recovers both corners cleanly instead.
    struct PoleZeroFit
    {
        double zeroHz = 0.0;
        double poleHz = 0.0;
        double gainDb = 0.0;
        double worstResidualDb = 0.0;
    };

    PoleZeroFit fitPoleZero (const std::vector<double>& frequencies,
                             const std::vector<double>& magnitudesDb,
                             double zeroLowHz, double zeroHighHz,
                             double poleLowHz, double poleHighHz)
    {
        PoleZeroFit best;
        auto bestCost = std::numeric_limits<double>::max();

        constexpr int steps = 240;

        for (int zi = 0; zi <= steps; ++zi)
        {
            const auto fz = zeroLowHz * std::pow (zeroHighHz / zeroLowHz,
                                                   static_cast<double> (zi) / steps);

            for (int pi = 0; pi <= steps; ++pi)
            {
                const auto fp = poleLowHz * std::pow (poleHighHz / poleLowHz,
                                                       static_cast<double> (pi) / steps);

                double offsetSum = 0.0;

                for (size_t i = 0; i < frequencies.size(); ++i)
                {
                    const auto rz = frequencies[i] / fz;
                    const auto rp = frequencies[i] / fp;
                    const auto shapeDb = 20.0 * std::log10 (rz / std::sqrt (1.0 + rz * rz))
                                          - 10.0 * std::log10 (1.0 + rp * rp);
                    offsetSum += magnitudesDb[i] - shapeDb;
                }

                const auto gainDb = offsetSum / static_cast<double> (frequencies.size());

                double cost = 0.0;
                double worst = 0.0;

                for (size_t i = 0; i < frequencies.size(); ++i)
                {
                    const auto rz = frequencies[i] / fz;
                    const auto rp = frequencies[i] / fp;
                    const auto modelDb = gainDb
                                          + 20.0 * std::log10 (rz / std::sqrt (1.0 + rz * rz))
                                          - 10.0 * std::log10 (1.0 + rp * rp);
                    const auto residual = magnitudesDb[i] - modelDb;
                    cost += residual * residual;
                    worst = std::max (worst, std::abs (residual));
                }

                if (cost < bestCost)
                {
                    bestCost = cost;
                    best.zeroHz = fz;
                    best.poleHz = fp;
                    best.gainDb = gainDb;
                    best.worstResidualDb = worst;
                }
            }
        }

        return best;
    }

    // Nominal small-signal pole of the feedback network at a given Drive.
    // NOTE (documented): this is NOT 1/(2*pi*R2*Cc). The diodes' own
    // small-signal resistance n*VT/(2*Is) (~9 MOhm) shunts R2, which pushes
    // the pole a few percent up - 5.66 kHz becomes ~6.0 kHz at Drive max.
    // That shift is circuit physics, not a discretisation artefact.
    double nominalPoleHz (double driveDb)
    {
        const auto d = juce::jlimit (0.0, 1.0, driveDb / 40.0);
        const auto r2 = FeedbackClipperStage::r2MinOhms + d * FeedbackClipperStage::r2RangeOhms;
        const auto rd = FeedbackClipperStage::emissionCoefficient * FeedbackClipperStage::thermalVoltage
                        / (2.0 * FeedbackClipperStage::isAmps);
        const auto parallel = r2 * rd / (r2 + rd);

        return 1.0 / (juce::MathConstants<double>::twoPi * parallel * FeedbackClipperStage::ccFarads);
    }
}

//==============================================================================
// T-F1
TEST_CASE ("T-F1: DC transfer and V_SCALE calibration", "[feedback][v030][dsp]")
{
    constexpr double sampleRate = 192000.0; // 4x oversampled 48 kHz

    SECTION ("settled DC transfer matches the offline DC solution over -5..+5 V")
    {
        // The offline DC solution of the circuit: the 47 nF input capacitor
        // makes the pre-emphasis a pure highpass, so In(DC) = 0, and the
        // steady state of
        //     g(y) = (1 + a)*y + R2*iD(y) - p,  p = (a - 1)*y - R2*iD(y)
        // reduces to y + R2*iD(y) = 0, whose unique root (g is strictly
        // monotone) is y = 0. The circuit therefore passes DC through
        // untouched: Vo = Vi + 0. Any leakage here would mean the
        // pre-emphasis or the trapezoidal update has a DC path it should
        // not have.
        const auto check = [&] (double fromVolts, double toVolts, double stepVolts)
        {
            FeedbackClipperStage stage;
            stage.prepare (sampleRate, 1);
            stage.setAsymmetry01 (0.0);

            double worstError = 0.0;

            for (double drive : { 0.0, 20.0, 40.0 })
            {
                stage.setDriveDb (drive);
                stage.snapSmoothingToTarget();

                for (double volts = fromVolts; volts <= toVolts + 1.0e-9; volts += stepVolts)
                {
                    stage.reset();
                    stage.setDriveDb (drive);
                    stage.snapSmoothingToTarget();

                    const auto input = static_cast<float> (volts / FeedbackClipperStage::voltageScale);
                    float output = 0.0f;

                    for (int i = 0; i < 600; ++i)
                        output = stage.processSample (0, input);

                    const auto outputVolts = static_cast<double> (output) * FeedbackClipperStage::voltageScale;
                    worstError = std::max (worstError, std::abs (outputVolts - volts));
                }
            }

            return worstError;
        };

        // Full span on a 10 mV grid, plus a 1 mV grid across the entire
        // diode-active region (a 1 mV grid over the full +/-5 V would be
        // 10001 x 3 x 600 solver calls - the same assertion for ~10x the
        // debug-build runtime).
        INFO ("D in {0, 0.5, 1}");
        CHECK (check (-5.0, 5.0, 0.010) < 1.0e-3);
        CHECK (check (-0.6, 0.6, 0.001) < 1.0e-3);
    }

    SECTION ("small-signal calibration: -60 dBFS at Drive 0 is essentially clean (THD < 0.1%)")
    {
        // -60 dBFS == 2 mV pk == ~22 mV across the feedback network at Drive
        // 0, safely below the diode knee. This is where the V_SCALE
        // calibration is pinned: if V_SCALE were an order of magnitude off,
        // this measurement would land either in the clipping region (THD in
        // the percent range) or in a numerically dead one.
        constexpr int fftSize = 12288; // 64 periods of 1 kHz at 192 kHz
        constexpr int bin = 64;

        const auto measurement = measureHarmonics (sampleRate, bin, fftSize, 0.002, 0.0, 0.0);

        INFO ("THD " << measurement.thdPercent << " %");
        CHECK (measurement.thdPercent < 0.1);
        CHECK (measurement.fundamental > 0.0);
    }

    SECTION ("hot programme: -12 dBFS at Drive 0 clips hard, BY DESIGN")
    {
        // The inverted assertion the revised brief calls for. -12 dBFS is
        // 0.5 V, roughly 9:1 past the knee once the stage's ~21.5 dB of
        // in-band gain is applied - the reference pedal's actual behaviour.
        // A clean result here means the solver or the calibration is broken.
        constexpr int fftSize = 12288;
        constexpr int bin = 64;

        const auto measured = measureHarmonics (sampleRate, bin, fftSize, 0.5, 0.0, 0.0);

        INFO ("THD " << measured.thdPercent << " %");
        CHECK (measured.thdPercent > 10.0);

        // ...and the harmonic structure is the CIRCUIT's, not a
        // discretisation artefact: the same solver at 32x the sample rate
        // must agree within 1 dB.
        const auto golden = measureHarmonics (48000.0 * 32.0, bin, fftSize * 8, 0.5, 0.0, 0.0);

        for (size_t h = 0; h < measured.harmonics.size(); ++h)
        {
            const auto measuredDbc = toDbc (measured.harmonics[h], measured.fundamental);
            const auto goldenDbc = toDbc (golden.harmonics[h], golden.fundamental);

            if (goldenDbc < -120.0)
                continue; // below the numerical floor in both runs

            INFO ("H" << (h + 2) << ": measured " << measuredDbc << " dBc, 32x golden " << goldenDbc << " dBc");
            CHECK (measuredDbc == Catch::Approx (goldenDbc).margin (1.0));
        }
    }
}

//==============================================================================
// T-F2
TEST_CASE ("T-F2: THD profile against a 32x-rate golden, and the asymmetry morph", "[feedback][v030][dsp]")
{
    constexpr double baseRate = 96000.0;
    constexpr double shippingRate = baseRate * 4.0;  // the shipping 4x configuration
    constexpr double finerRate = baseRate * 8.0;     // the 8x option, for the convergence check
    constexpr double goldenRate = baseRate * 32.0;   // numerical ground truth
    constexpr int fftSize = 3840;                    // 10 periods of 1 kHz at 384 kHz
    constexpr int bin = 10;

    SECTION ("the harmonic series tracks the 32x golden, and converges towards it with rate")
    {
        // The residual measured here is the trapezoidal rule's own O(h^2)
        // truncation error. It turns out to be tiny - hundredths of a dB
        // even on H10 at full Drive - because the in-loop pole sits at
        // 5.7-61 kHz, comfortably resolved at 384 kHz. The 8x run is
        // included so the bound is shown to hold across the shipping
        // oversampling options rather than at one lucky rate.
        for (const double amplitudeVolts : { 0.1, 0.2 })
        {
            for (const double driveDb : { 0.0, 20.0, 40.0 })
            {
                INFO ("amplitude " << amplitudeVolts << " V, Drive " << driveDb << " dB");

                // The fundamental must stay at 1 kHz at every rate, so the
                // BIN index is fixed and only the record length scales:
                // f0 = bin * fs / N, and both fs and N carry the same factor.
                const auto golden = measureHarmonics (goldenRate, bin, fftSize * 8,
                                                      amplitudeVolts, driveDb, 0.0);
                REQUIRE (golden.fundamental > 0.0);

                const auto worstErrorAgainstGolden = [&] (double rate, int rateFactor, int fromHarmonic, int toHarmonic)
                {
                    const auto measured = measureHarmonics (rate, bin, fftSize * rateFactor,
                                                            amplitudeVolts, driveDb, 0.0);
                    REQUIRE (measured.fundamental > 0.0);

                    double worst = 0.0;

                    for (int order = fromHarmonic; order <= toHarmonic; ++order)
                    {
                        const auto index = static_cast<size_t> (order - 2);
                        const auto goldenDbc = toDbc (golden.harmonics[index], golden.fundamental);

                        if (goldenDbc < -100.0)
                            continue; // below the numerical floor - nothing to compare

                        const auto measuredDbc = toDbc (measured.harmonics[index], measured.fundamental);
                        worst = std::max (worst, std::abs (measuredDbc - goldenDbc));
                    }

                    return worst;
                };

                const auto lowOrderAt4x = worstErrorAgainstGolden (shippingRate, 1, 2, 5);
                const auto highOrderAt4x = worstErrorAgainstGolden (shippingRate, 1, 6, 10);
                const auto allAt4x = std::max (lowOrderAt4x, highOrderAt4x);
                const auto allAt8x = std::max (worstErrorAgainstGolden (finerRate, 2, 2, 5),
                                               worstErrorAgainstGolden (finerRate, 2, 6, 10));

                INFO ("worst deviation from the 32x golden: H2-H5 at 4x " << lowOrderAt4x
                      << " dB, H6-H10 at 4x " << highOrderAt4x << " dB, all at 8x " << allAt8x << " dB");

                CHECK (lowOrderAt4x <= 1.0);
                CHECK (highOrderAt4x <= 1.0);
                CHECK (allAt4x <= 1.0);
                CHECK (allAt8x <= 1.0);
            }
        }
    }

    SECTION ("Asymmetry 0%: the diode law is symmetric, so even harmonics vanish")
    {
        const auto measured = measureHarmonics (shippingRate, bin, fftSize, 0.2, 40.0, 0.0);

        for (size_t h = 0; h < measured.harmonics.size(); ++h)
        {
            const auto order = h + 2;

            if (order % 2 != 0)
                continue;

            const auto dbc = toDbc (measured.harmonics[h], measured.fundamental);
            INFO ("H" << order << ": " << dbc << " dBc");
            CHECK (dbc < -80.0);
        }
    }

    SECTION ("Asymmetry 100%: H2 rises above -40 dBc at full Drive")
    {
        const auto measured = measureHarmonics (shippingRate, bin, fftSize, 0.2, 40.0, 1.0);

        const auto h2Dbc = toDbc (measured.harmonics[0], measured.fundamental);
        INFO ("H2 " << h2Dbc << " dBc");
        CHECK (h2Dbc > -40.0);
    }
}

//==============================================================================
// T-F3 / T-F5 share one small-signal measurement.
TEST_CASE ("T-F3/T-F5: the small-signal response matches the analytic circuit model", "[feedback][v030][dsp]")
{
    constexpr double sampleRate = 192000.0;
    constexpr int fftSize = 65536;
    const auto binHz = sampleRate / static_cast<double> (fftSize);

    // Log-spaced, snapped to exact DFT bins so every measurement is
    // leakage-free.
    std::vector<int> bins;
    for (int i = 0; i <= 44; ++i)
    {
        const auto frequency = 20.0 * std::pow (10.0, static_cast<double> (i) / 44.0 * 3.0); // 20 Hz .. 20 kHz
        const auto bin = std::max (1, static_cast<int> (std::lround (frequency / binHz)));

        if (bins.empty() || bin != bins.back())
            bins.push_back (bin);
    }

    // The stimulus level has to keep the diode strictly in its linear
    // region, and "linear" depends on Drive: the stage's own in-band gain is
    // ~11x at Drive 0 but ~110x at Drive max, so a level that is small
    // signal at one end is 5 dB into the knee at the other. 2 mV / 20 uV
    // keeps |V| well under n*VT at both.
    const auto amplitudeFor = [] (double driveDb) { return driveDb < 20.0 ? 0.002 : 0.00002; };

    for (const double driveDb : { 0.0, 40.0 })
    {
        INFO ("Drive " << driveDb << " dB");

        FeedbackClipperStage reference;
        reference.prepare (sampleRate, 1);

        std::vector<double> frequencies;
        std::vector<double> feedbackTermDb;

        double worstTotalDb = 0.0;

        for (const auto bin : bins)
        {
            const auto frequency = static_cast<double> (bin) * binHz;
            const auto measured = measureResponse (sampleRate, bin, fftSize, amplitudeFor (driveDb), driveDb);
            const auto analytic = reference.analyticSmallSignalResponse (frequency, driveDb);

            frequencies.push_back (frequency);
            feedbackTermDb.push_back (20.0 * std::log10 (std::abs (measured - 1.0) + 1.0e-30));

            // T-F5: absolute-gain identity against the analytic
            // bilinear-mapped 1 + Z2(s)/Z1(s), 20 Hz - 10 kHz. The clean
            // path is unity only at DC - in band the stage really is a
            // ~12x/~110x amplifier - so this is an absolute-gain check, not
            // a "does it pass the signal through" check.
            if (frequency <= 10000.0)
            {
                const auto measuredDb = 20.0 * std::log10 (std::abs (measured));
                const auto analyticDb = 20.0 * std::log10 (std::abs (analytic));
                worstTotalDb = std::max (worstTotalDb, std::abs (measuredDb - analyticDb));

                INFO ("f = " << frequency << " Hz: measured " << measuredDb << " dB, analytic " << analyticDb << " dB");
                CHECK (measuredDb == Catch::Approx (analyticDb).margin (0.25));
            }
        }

        INFO ("worst |measured - analytic| over 20 Hz - 10 kHz: " << worstTotalDb << " dB");

        // T-F3: recover the 720 Hz pre-emphasis zero and the feedback pole
        // from the measured curve by fitting the circuit's own one-zero /
        // one-pole shape.
        const auto fit = fitPoleZero (frequencies, feedbackTermDb, 300.0, 1800.0, 2000.0, 300000.0);

        INFO ("fit: zero " << fit.zeroHz << " Hz, pole " << fit.poleHz << " Hz, gain "
              << fit.gainDb << " dB, worst residual " << fit.worstResidualDb << " dB");

        // The model must actually describe the measurement, or the recovered
        // corners mean nothing.
        CHECK (fit.worstResidualDb < 0.5);

        // fz = 1/(2*pi*R1*Cz), straight from the component values.
        const auto nominalZeroHz = 1.0 / (juce::MathConstants<double>::twoPi
                                          * FeedbackClipperStage::r1Ohms * FeedbackClipperStage::czFarads);
        INFO ("nominal zero " << nominalZeroHz << " Hz");
        CHECK (fit.zeroHz == Catch::Approx (nominalZeroHz).epsilon (0.05));

        if (driveDb == 0.0)
        {
            // In-band gain of the feedback term alone at Drive 0 is
            // 20*log10((R2||Rd)/R1) ~ 20.6 dB, i.e. ~21.5 dB once the unity
            // clean path is added back.
            INFO ("in-band feedback-term gain " << fit.gainDb << " dB");
            CHECK (fit.gainDb == Catch::Approx (20.6).margin (0.5));
        }
        else
        {
            // The pole is only recoverable where it sits inside the measured
            // band: at Drive 0 it is at ~61 kHz, a third of the way to
            // Nyquist even at 192 kHz, and the sweep stops at 20 kHz.
            INFO ("nominal pole " << nominalPoleHz (driveDb) << " Hz (R2*Cc alone would give "
                  << (1.0 / (juce::MathConstants<double>::twoPi
                             * (FeedbackClipperStage::r2MinOhms + FeedbackClipperStage::r2RangeOhms)
                             * FeedbackClipperStage::ccFarads))
                  << " Hz - the diodes' ~9 MOhm small-signal resistance shunts R2)");
            CHECK (fit.poleHz == Catch::Approx (nominalPoleHz (driveDb)).epsilon (0.05));
        }
    }
}

//==============================================================================
// T-F4
TEST_CASE ("T-F4: the safeguarded Newton solver never exceeds 8 iterations and never produces a "
           "non-finite sample, anywhere on a hostile grid",
           "[feedback][v030][dsp][robustness]")
{
    // +/-10 V is five times full scale - a level the plugin can only reach
    // if a host feeds it something pathological, which is exactly when a
    // circuit solver has to stay bounded. Strict monotonicity of g(y)
    // guarantees the bisection fallback terminates; this sweep is what turns
    // that argument into a measurement.
    int worstIterations = 0;
    bool allFinite = true;

    for (const double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const double factor : { 2.0, 4.0, 8.0 })
        {
            const auto oversampledRate = sampleRate * factor;

            for (const double drive : { 0.0, 10.0, 20.0, 30.0, 40.0 })
            {
                for (const double asymmetry : { 0.0, 0.5, 1.0 })
                {
                    FeedbackClipperStage stage;
                    stage.prepare (oversampledRate, 1);
                    stage.setDriveDb (drive);
                    stage.setAsymmetry01 (asymmetry);
                    stage.snapSmoothingToTarget();

                    // Alternating extremes, a slow ramp, and hard steps
                    // through zero - the three shapes that stress a warm
                    // started Newton solve in different directions.
                    for (int i = 0; i < 4096; ++i)
                    {
                        double volts;

                        if (i < 1024)
                            volts = (i % 2 == 0) ? 10.0 : -10.0;
                        else if (i < 2048)
                            volts = -10.0 + 20.0 * static_cast<double> (i - 1024) / 1024.0;
                        else if (i < 3072)
                            volts = ((i / 7) % 2 == 0) ? 10.0 : -10.0;
                        else
                            volts = 10.0 * std::sin (0.37 * static_cast<double> (i));

                        const auto output = stage.processSample (
                            0, static_cast<float> (volts / FeedbackClipperStage::voltageScale));

                        worstIterations = std::max (worstIterations, stage.getLastIterationCount());

                        if (! std::isfinite (output))
                            allFinite = false;
                    }
                }
            }
        }
    }

    INFO ("worst Newton iteration count: " << worstIterations);
    CHECK (allFinite);
    CHECK (worstIterations <= 8);
    CHECK (worstIterations > 0); // the solver really ran (guards a silently short-circuited path)
}

//==============================================================================
TEST_CASE ("FeedbackClipperStage: reset() clears all state", "[feedback][v030][dsp]")
{
    FeedbackClipperStage stage;
    stage.prepare (192000.0, 2);
    stage.setDriveDb (20.0);
    stage.snapSmoothingToTarget();

    for (int i = 0; i < 512; ++i)
        for (int channel = 0; channel < 2; ++channel)
            stage.processSample (channel, 0.5f);

    stage.reset();
    stage.setDriveDb (20.0);
    stage.snapSmoothingToTarget();

    // A cleared stage fed silence must produce exact silence - if any state
    // survived, the trapezoidal update would ring it out.
    for (int i = 0; i < 64; ++i)
        for (int channel = 0; channel < 2; ++channel)
            CHECK (stage.processSample (channel, 0.0f) == 0.0f);
}
