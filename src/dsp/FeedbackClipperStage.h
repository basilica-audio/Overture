#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

// Overture v0.3.0's "Feedback" voicing: a genuinely circuit-solved
// feedback-clipper stage, replacing the Bite-shelf -> static-waveshaper path
// with a trapezoidally-discretised, Newton-solved ODE of the op-amp /
// anti-parallel-diode / RC feedback loop that a reference-class tight boost
// is actually built from.
//
// Why this is not another waveshaper: the loop's 51 pF feedback capacitor
// puts a drive-DEPENDENT lowpass pole *inside* the nonlinearity (about
// 61 kHz at Drive 0, about 5.7 kHz at Drive max), and the diodes sit in the
// feedback path rather than shunting the output, so the stage's gain, its
// knee, and its high-frequency rounding all move together with the signal
// and the Drive control. That is memory: no memoryless transfer curve, at
// any oversampling factor, reproduces it.
//
// Topology (runs inside the existing oversampler):
//
//   Vi -> [linear pre-emphasis  In(s) = Vi * s / (R1 * (s + wz)),
//          fz = 1/(2*pi*R1*Cz) ~ 720 Hz]
//      -> [1-state nonlinear ODE, trapezoidal + safeguarded Newton]
//      -> Vo = Vi + V     (unity clean path + clipped difference)
//
// ODE (state V = the voltage across the feedback network):
//
//   dV/dt = In/Cc - V/(R2*Cc) - iD(V)/Cc
//
// with the (asymmetry-morphable) diode law
//
//   iD(v) = Is * (exp(v / (mf*n*VT)) - exp(-v / (n*VT))),   mf = 1 + asym01
//
// which collapses to the symmetric 2*Is*sinh(v/(n*VT)) at asymmetry 0.
//
// Trapezoidal discretisation (multiply through by R2*Cc, substitute
// a = 2*R2*Cc*fs) gives the implicit per-sample equation solved below:
//
//   g(y)  = (1 + a)*y + R2*iD(y) - p = 0
//   g'(y) = (1 + a)   + R2*iD'(y)
//   p     = (a - 1)*V[n-1] + R2*(In[n] + In[n-1]) - R2*iD(V[n-1])
//
// g is strictly monotonically increasing in y (both the linear term and iD
// are), so the root is unique and a bisection fallback is guaranteed to
// terminate - which is what makes the hard 8-iteration cap in
// tests/FeedbackClipperTests.cpp (T-F4) safe rather than optimistic.
//
// References: Yeh, Abel & Smith, "Simulation of the diode limiter in guitar
// distortion circuits by numerical solution of ordinary differential
// equations" (DAFx-07); the same trapezoidal/Newton formulation used for
// K-method and DK-method circuit solvers. All equations are implemented
// first-party here - nothing is vendored.
//
// Numerics: solver state and every intermediate are `double`; the block I/O
// stays `float`. Real-time safe: prepare() sizes the per-channel state,
// processSample() only does arithmetic.
namespace basilica::dsp
{
    class FeedbackClipperStage
    {
    public:
        //======================================================================
        // Frozen circuit constants (brief SS3.2 / docs/architecture.md).

        static constexpr double r1Ohms = 4700.0;          // input/pre-emphasis resistor
        static constexpr double czFarads = 47.0e-9;       // pre-emphasis capacitor -> fz ~ 720 Hz
        static constexpr double ccFarads = 51.0e-12;      // feedback capacitor (the in-loop pole)
        static constexpr double r2MinOhms = 51000.0;      // feedback resistor at Drive 0
        static constexpr double r2RangeOhms = 500000.0;   // added by the Drive pot at Drive max
        static constexpr double isAmps = 2.52e-9;         // diode saturation current
        static constexpr double emissionCoefficient = 1.75;
        static constexpr double thermalVoltage = 0.02585; // VT at ~27 C

        // Full-scale <-> volts calibration: 0 dBFS == 2.0 Vpk, so -12 dBFS
        // is 0.5 V (hot-pickup territory). NOTE (brief SS3.2, binding): with
        // R1/R2 as above this stage has ~21.5 dB of minimum in-band
        // small-signal gain into a ~0.35-0.45 V diode knee, so its linear
        // region at Drive 0 ends around -40...-35 dBFS. This voicing is a
        // touch-sensitive, PROGRAMME-LEVEL clipper by design, not a
        // clean-at-unity boost - a -12 dBFS programme overdrives the knee
        // roughly 9:1 and clips with THD in the tens of percent. That is the
        // reference pedal's actual behaviour and is asserted as such in
        // tests/FeedbackClipperTests.cpp (T-F1).
        static constexpr double voltageScale = 2.0;

        // Single documented output trim (NOT a parameter, not persisted) for
        // pre-release perceived-level matching against the legacy voicings.
        // 0 dB ships in v0.3.0; see brief SS7 R4.
        static constexpr double outputTrimDb = 0.0;

        // Argument clamp inside exp() - |v/(n*VT)| beyond this is physically
        // unreachable (it would be several volts across a silicon diode) and
        // only ever arises transiently inside the Newton search.
        static constexpr double diodeArgumentLimit = 40.0;

        // Multiplicative smoothing time for the Drive-dependent feedback
        // resistance. Smoothing R2 in the resistance domain (rather than
        // smoothing the normalised Drive) is what keeps the in-loop pole and
        // the loop gain moving together the way the physical pot does.
        static constexpr double resistanceSmoothingSeconds = 0.05;

        //======================================================================
        void prepare (double newSampleRate, int numChannels)
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
            channels.assign (static_cast<size_t> (std::max (1, numChannels)), ChannelState {});

            // Bilinear-mapped pre-emphasis (TPT one-pole highpass at fz),
            // so the measured pole/zero placement matches the analytic
            // 1 + Z2(s)/Z1(s) prediction the tests compare against.
            const auto fz = 1.0 / (juce::MathConstants<double>::twoPi * r1Ohms * czFarads);
            preEmphasisG = std::tan (juce::MathConstants<double>::pi * fz / sampleRate);

            smoothedR2 = targetR2;
            resistanceSmoothingCoefficient = std::exp (-1.0 / (resistanceSmoothingSeconds * sampleRate));
            outputTrimGain = std::pow (10.0, outputTrimDb / 20.0);

            reset();
        }

        void reset() noexcept
        {
            for (auto& channel : channels)
                channel = ChannelState {};

            smoothedR2 = targetR2;
            lastIterationCount = 0;
        }

        // Drive 0-40 dB maps to D = drive/40 -> R2 = R2min + D*R2range.
        void setDriveDb (double driveDb) noexcept
        {
            const auto d = juce::jlimit (0.0, 1.0, driveDb / 40.0);
            targetR2 = r2MinOhms + d * r2RangeOhms;
        }

        // 0-1: morphs the diode law from the symmetric sinh (0) towards the
        // two-series/one-reverse asymmetric variant (1), which roughly
        // doubles the forward threshold and therefore generates strong even
        // harmonics.
        void setAsymmetry01 (double newAsymmetry01) noexcept
        {
            asymmetry01 = juce::jlimit (0.0, 1.0, newAsymmetry01);
        }

        // Snaps the smoothed feedback resistance to its target - used when
        // the engine (re)prepares so the first block already runs at the
        // requested Drive rather than sweeping up from the previous value.
        void snapSmoothingToTarget() noexcept { smoothedR2 = targetR2; }

        int getNumChannels() const noexcept { return static_cast<int> (channels.size()); }

        // Newton iterations consumed by the most recent processSample()
        // call - asserted against the hard cap by T-F4.
        int getLastIterationCount() const noexcept { return lastIterationCount; }

        // Solves one sample for one channel. `x` is in full-scale units; the
        // returned value is too (the volts conversion is internal).
        float processSample (int channel, float x) noexcept
        {
            if (channel < 0 || channel >= static_cast<int> (channels.size()))
                return x;

            auto& state = channels[static_cast<size_t> (channel)];

            // Multiplicative (resistance-domain) smoothing of R2, advanced
            // once per sample frame (on channel 0) so a stereo instance
            // smooths at exactly the same rate as a mono one.
            if (channel == 0)
                smoothedR2 = targetR2 + resistanceSmoothingCoefficient * (smoothedR2 - targetR2);

            const auto r2 = smoothedR2;

            const auto vi = static_cast<double> (x) * voltageScale;

            // Pre-emphasis: TPT highpass s/(s+wz), then scaled by 1/R1 to
            // become the current injected into the feedback node.
            const auto v = (vi - state.preEmphasisState) * preEmphasisG / (1.0 + preEmphasisG);
            const auto lowpass = v + state.preEmphasisState;
            state.preEmphasisState = lowpass + v;
            const auto in = (vi - lowpass) / r1Ohms;

            // Trapezoidal right-hand side.
            const auto a = 2.0 * r2 * ccFarads * sampleRate;
            const auto vPrev = state.capacitorVolts;
            const auto p = (a - 1.0) * vPrev + r2 * (in + state.previousIn) - r2 * diodeCurrent (vPrev);
            state.previousIn = in;

            const auto y = solve (p, a, r2, vPrev);
            state.capacitorVolts = y;

            const auto vo = (vi + y) * outputTrimGain;
            return static_cast<float> (vo / voltageScale);
        }

        // Analytic small-signal (linearised) DIGITAL transfer function of the
        // whole stage at the given normalised frequency, evaluated from the
        // exact same discretisation the solver uses (bilinear pre-emphasis +
        // trapezoidal ODE). tests/FeedbackClipperTests.cpp T-F5 compares the
        // measured response against this, and T-F3 extracts the pole/zero
        // from it - so the reference is the model, not a hand-typed number.
        std::complex<double> analyticSmallSignalResponse (double frequencyHz, double driveDb) const
        {
            const auto d = juce::jlimit (0.0, 1.0, driveDb / 40.0);
            const auto r2 = r2MinOhms + d * r2RangeOhms;
            const auto a = 2.0 * r2 * ccFarads * sampleRate;

            // Small-signal diode conductance at V = 0: d/dV [2*Is*sinh(V/(n*VT))]
            // = 2*Is/(n*VT). b is that conductance normalised by R2.
            const auto b = 2.0 * r2 * isAmps / (emissionCoefficient * thermalVoltage);

            const auto omega = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;
            const std::complex<double> zInverse (std::cos (omega), -std::sin (omega));

            const auto preEmphasis = (1.0 / r1Ohms) * (1.0 - zInverse)
                                     / ((1.0 + preEmphasisG) + (preEmphasisG - 1.0) * zInverse);

            const auto loop = r2 * (1.0 + zInverse)
                              / ((1.0 + a + b) - (a - 1.0 - b) * zInverse);

            return (1.0 + preEmphasis * loop) * outputTrimGain;
        }

        double getSampleRate() const noexcept { return sampleRate; }

    private:
        struct ChannelState
        {
            double preEmphasisState = 0.0;
            double previousIn = 0.0;
            double capacitorVolts = 0.0;
        };

        double diodeCurrent (double v) const noexcept
        {
            const auto forwardScale = 1.0 + asymmetry01;
            const auto u = v / (emissionCoefficient * thermalVoltage);
            const auto forward = juce::jlimit (-diodeArgumentLimit, diodeArgumentLimit, u / forwardScale);
            const auto reverse = juce::jlimit (-diodeArgumentLimit, diodeArgumentLimit, -u);

            return isAmps * (std::exp (forward) - std::exp (reverse));
        }

        double diodeConductance (double v) const noexcept
        {
            const auto forwardScale = 1.0 + asymmetry01;
            const auto nvt = emissionCoefficient * thermalVoltage;
            const auto u = v / nvt;
            const auto forward = juce::jlimit (-diodeArgumentLimit, diodeArgumentLimit, u / forwardScale);
            const auto reverse = juce::jlimit (-diodeArgumentLimit, diodeArgumentLimit, -u);

            return isAmps * (std::exp (forward) / (forwardScale * nvt) + std::exp (reverse) / nvt);
        }

        // Safeguarded Newton with a guaranteed, DIODE-AWARE bracket.
        //
        // g(0) = -p and g is strictly increasing, so the root always lies
        // between 0 and the point where either term of g alone already
        // reaches p. Both give a valid bound, and taking the tighter one
        // matters enormously here: the purely linear bound p/(1+a) can be
        // tens of volts on a hot transient, while the true root never leaves
        // the +/-0.7 V a silicon junction can hold. Starting Newton from a
        // bracket that wide walks it straight into the exp() argument clamp,
        // where the derivative saturates and the iteration creeps by ~n*VT
        // per step - it then runs out of its iteration budget nowhere near
        // the root and poisons the next sample's right-hand side.
        //
        // For y > 0, exp(-y/(n*VT)) <= 1, so
        //     g(y) >= (1 + a)*y + R2*Is*(exp(y/(mf*n*VT)) - 1) - p
        // and each term reaching p on its own bounds the root from above:
        //     yMax = min( p/(1+a),  mf*n*VT*ln(1 + p/(R2*Is)) ).
        // The y < 0 branch is the mirror image with mf = 1 (the reverse
        // diode is never scaled by the asymmetry morph).
        //
        // Newton steps that would leave the bracket are replaced by a
        // bisection step; g is strictly monotone, so that fallback is
        // guaranteed to terminate, which is what makes the hard 8-iteration
        // cap safe (T-F4).
        double solve (double p, double a, double r2, double warmStart) noexcept
        {
            const auto nvt = emissionCoefficient * thermalVoltage;
            const auto forwardScale = 1.0 + asymmetry01;
            const auto r2Is = r2 * isAmps;

            double low = 0.0;
            double high = 0.0;

            if (p >= 0.0)
            {
                high = std::min (p / (1.0 + a), forwardScale * nvt * std::log1p (p / r2Is));
            }
            else
            {
                low = -std::min (-p / (1.0 + a), nvt * std::log1p (-p / r2Is));
            }

            auto y = juce::jlimit (low, high, warmStart);

            constexpr int maxIterations = 8;

            // Relative residual: p spans microvolts (small signal) to tens
            // of volts (a hot transient into a low feedback resistance), so
            // an absolute tolerance would be unreachable at one end and
            // wastefully strict at the other.
            const auto residualTolerance = 1.0e-12 * std::max (1.0, std::abs (p));
            constexpr double stepTolerance = 1.0e-13;

            int iteration = 0;

            for (; iteration < maxIterations; ++iteration)
            {
                const auto residual = (1.0 + a) * y + r2 * diodeCurrent (y) - p;

                if (residual > 0.0)
                    high = y;
                else
                    low = y;

                if (std::abs (residual) < residualTolerance)
                    break;

                const auto derivative = (1.0 + a) + r2 * diodeConductance (y);
                auto next = y - residual / derivative;

                if (! (next > low && next < high) || ! std::isfinite (next))
                    next = 0.5 * (low + high);

                const auto step = std::abs (next - y);
                y = next;

                if (step < stepTolerance)
                {
                    ++iteration;
                    break;
                }
            }

            lastIterationCount = iteration;
            return std::isfinite (y) ? y : 0.0;
        }

        double sampleRate = 48000.0;
        double preEmphasisG = 0.0;
        double targetR2 = r2MinOhms;
        double smoothedR2 = r2MinOhms;
        double resistanceSmoothingCoefficient = 0.0;
        double asymmetry01 = 0.0;
        double outputTrimGain = 1.0;
        int lastIterationCount = 0;

        std::vector<ChannelState> channels;
    };
}
