#pragma once

#include "EnvelopeFollower.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

// Overture v0.3.0's built-in noise gate: a Precision-Drive-class, program-
// dependent downward gate designed for the one job the plugin's whole
// reason for existing depends on - silence between palm-muted, low-tuned
// chugs.
//
// Architecture (all control-path math per-sample in the log/dB domain, the
// placement recommended by Giannoulis, Massberg & Reiss, "Digital Dynamic
// Range Compressor Design - A Tutorial and Analysis", JAES 60(6), 2012):
//
//   x -> [sidechain HPF 100 Hz + LPF 5 kHz, 2nd-order TPT SVF]
//     -> [exponential mean-square detector, tau = 5 ms] -> 10*log10 -> dB
//     -> [non-linear-capacitor (NLC) anti-chatter smoother]
//     -> [state machine: T_open / T_close = T_open - H, retriggering hold]
//     -> [target gain] -> [0.1 ms one-pole open ramp | dB-LINEAR close ramp]
//     -> 10^(g/20) -> multiply
//
// Two details are what separate this from a textbook gate and are the
// reason the class exists at all rather than being a juce::dsp::NoiseGate
// call:
//
//   1. The NLC smoother is a digital equivalent of the non-linear capacitor
//      in the THAT DN100-class hardware detector: its time constant
//      collapses from tau_slow to tau_fast as the *size* of the level step
//      grows, so genuine note attacks pass through untouched while the
//      residual mean-square ripple of a low-frequency note (a 70 Hz open
//      string is the worst case at tau_rms = 5 ms) is smoothed away instead
//      of chattering the state machine.
//
//   2. The closing ramp is dB-LINEAR (g[n] = max(g[n-1] - S/fs, -M)), the
//      VCA/DN100 signature, not the exponential-in-linear-gain ramp most
//      digital gates use. A dB-linear ramp reaches the floor in a bounded,
//      predictable time and sounds like a fader being pulled rather than a
//      tail being squashed - see tests/NoiseGateTests.cpp T-G4, which fits a
//      line to 20*log10(gain) and rejects an exponential.
//
// The release slope S is program-dependent when the Auto mode is selected
// ("TVP", after the linearized-transient-vari-program release the hardware
// class markets): two envelopes are run on the detector output, a fast one
// and a slow one; when the slow one gets more than W_tvp dB above the fast
// one the note has stopped abruptly (a staccato mute), so the slow envelope
// is dumped to the fast one and the gate releases at S_fast; otherwise the
// gate releases at the note's own measured decay rate plus a small margin,
// so a ringing chord is never truncated.
//
// Zero added latency (no lookahead in v0.3.0 - see docs/architecture.md's
// roadmap note), no oversampling and no anti-derivative anti-aliasing: the
// gate is a multiplicative gain, not a waveshaper, and its gain signal is
// bandlimited by the ramps above.
//
// Real-time safety: prepare() sizes everything; process() and reset() never
// allocate.
namespace basilica::dsp
{
    // 2nd-order topology-preserving-transform state-variable filter
    // (Zavalishin, "The Art of VA Filter Design", ch. 4). Used only in the
    // gate's sidechain, where its unconditional stability under per-sample
    // coefficient changes and its exact cutoff at any sample rate matter
    // more than matching the RBJ shapes the audio path uses.
    class TptSvf
    {
    public:
        enum class Mode
        {
            lowpass,
            highpass
        };

        void setMode (Mode newMode) noexcept { mode = newMode; }

        void setCutoff (double cutoffHz, double sampleRate, double q) noexcept
        {
            const auto nyquist = 0.5 * sampleRate;
            const auto safeCutoff = juce::jlimit (1.0, nyquist * 0.99, cutoffHz);

            g = std::tan (juce::MathConstants<double>::pi * safeCutoff / sampleRate);
            k = 1.0 / std::max (1.0e-6, q);
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void reset() noexcept
        {
            ic1eq = 0.0;
            ic2eq = 0.0;
        }

        double process (double x) noexcept
        {
            const auto v3 = x - ic2eq;
            const auto v1 = a1 * ic1eq + a2 * v3;
            const auto v2 = ic2eq + a2 * ic1eq + a3 * v3;

            ic1eq = 2.0 * v1 - ic1eq;
            ic2eq = 2.0 * v2 - ic2eq;

            return mode == Mode::lowpass ? v2 : (x - k * v1 - v2);
        }

    private:
        Mode mode = Mode::lowpass;
        double g = 0.0, k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
        double ic1eq = 0.0, ic2eq = 0.0;
    };

    class NoiseGate
    {
    public:
        // Release behaviour selector, mirroring ParamIDs::gateRelease's
        // choice list order exactly (Auto = 0, Fast = 1, Slow = 2).
        enum class ReleaseMode
        {
            automatic = 0,
            fast = 1,
            slow = 2
        };

        //======================================================================
        // Frozen, non-parameter constants (brief SS4 "New non-parameter
        // constants"; docs/architecture.md carries the same table). These are
        // hardware-derived (THAT DN100 anatomy / linearized-TVP patent
        // ranges), not user controls - they can be re-tuned pre-release
        // without any state-schema impact because nothing persists them.

        // Hysteresis: the closing threshold sits H dB below the opening one.
        static constexpr double hysteresisDb = 4.0;

        // Retriggering hold: how long the gate stays fully open after the
        // detector last fell below T_close.
        static constexpr double holdSeconds = 0.020;

        // Range/floor: how far down a fully closed gate pulls the signal.
        static constexpr double rangeDb = 90.0;

        // Opening ramp time constant (one-pole on the rising gain branch).
        static constexpr double attackTauSeconds = 0.0001;

        // Detector mean-square time constant.
        static constexpr double detectorTauSeconds = 0.005;

        // NLC anti-chatter smoother.
        static constexpr double nlcTauFastSeconds = 0.002;
        static constexpr double nlcTauSlowSeconds = 0.030;
        static constexpr double nlcKneeDb = 1.5;

        // Sidechain band-limiting (detector only - the audio path is never
        // filtered by the gate).
        static constexpr double sidechainHighPassHz = 100.0;
        static constexpr double sidechainLowPassHz = 5000.0;

        // TVP dual-envelope auto-release.
        static constexpr double tvpFastAttackTauSeconds = 0.0005;
        static constexpr double tvpFastReleaseTauSeconds = 0.010;
        static constexpr double tvpSlowSlopeDbPerSecond = 40.0;
        static constexpr double tvpWindowDb = 4.0;
        static constexpr double tvpFastSlopeDbPerSecond = 1000.0;
        static constexpr double tvpTrackMarginDbPerSecond = 15.0;
        static constexpr double tvpTrackMaxDbPerSecond = 500.0;
        static constexpr double tvpDecayEstimateTauSeconds = 0.050;

        // Fixed release slopes for the two non-automatic modes.
        static constexpr double fastSlopeDbPerSecond = 800.0;
        static constexpr double slowSlopeDbPerSecond = 60.0;

        // Threshold smoothing (brief SS4: 20 ms linear on the dB target).
        static constexpr double thresholdSmoothingSeconds = 0.020;

        // Grace window after reset()/requestSeed(): the gate is forced fully
        // open while the mean-square detector (tau 5 ms) and the NLC smoother
        // converge on the actual programme level. Without it, engaging the
        // gate on a sustained note would mute the first few milliseconds
        // (the detector starts from digital silence and has to climb), and
        // engaging it at a waveform zero-crossing would slam the gate shut
        // on a signal that is in fact well above threshold. 30 ms is 6x the
        // detector time constant and comfortably covers the NLC's fast
        // branch as well.
        static constexpr double seedGraceSeconds = 0.030;

        //======================================================================
        void prepare (double newSampleRate, int numChannels)
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

            sidechainHighPass.setMode (TptSvf::Mode::highpass);
            sidechainHighPass.setCutoff (sidechainHighPassHz, sampleRate, juce::MathConstants<double>::sqrt2 * 0.5);
            sidechainLowPass.setMode (TptSvf::Mode::lowpass);
            sidechainLowPass.setCutoff (sidechainLowPassHz, sampleRate, juce::MathConstants<double>::sqrt2 * 0.5);

            detector.setCoefficient (onePoleCoefficient (detectorTauSeconds, sampleRate));

            nlcFastCoefficient = 1.0 - onePoleCoefficient (nlcTauFastSeconds, sampleRate);
            nlcSlowCoefficient = 1.0 - onePoleCoefficient (nlcTauSlowSeconds, sampleRate);

            tvpFast.setCoefficients (onePoleCoefficient (tvpFastAttackTauSeconds, sampleRate),
                                     onePoleCoefficient (tvpFastReleaseTauSeconds, sampleRate));

            decayEstimateCoefficient = onePoleCoefficient (tvpDecayEstimateTauSeconds, sampleRate);
            attackCoefficient = 1.0 - onePoleCoefficient (attackTauSeconds, sampleRate);

            holdSamples = static_cast<int> (std::lround (holdSeconds * sampleRate));
            seedGraceSamples = static_cast<int> (std::lround (seedGraceSeconds * sampleRate));
            thresholdStepDb = sampleRate * thresholdSmoothingSeconds > 0.0
                                  ? (60.0 / (sampleRate * thresholdSmoothingSeconds))
                                  : 60.0;

            juce::ignoreUnused (numChannels);
            reset();
        }

        // Clears every detector/envelope/ramp state without deallocating.
        // Safe from the audio thread. The gate comes up fully open (gain
        // 0 dB) and requesting a seed, so the first processed sample decides
        // open/closed from the actual programme level rather than fading in
        // from the floor.
        void reset() noexcept
        {
            sidechainHighPass.reset();
            sidechainLowPass.reset();
            detector.reset (0.0);
            tvpFast.reset (-160.0);

            nlcDb = -160.0;
            slowEnvelopeDb = -160.0;
            decayEstimateDbPerSecond = 0.0;
            gainDb = 0.0;
            currentGain = 1.0f;
            gateOpen = true;
            fastReleaseLatched = false;
            holdCounter = 0;
            stateTransitions = 0;
            seedCountdown = seedGraceSamples;
            smoothedThresholdDb = requestedThresholdDb;
        }

        // Discrete controls. Real-time safe; consumed by the per-sample
        // state machine on the next processed sample.
        void setThresholdDb (double newThresholdDb) noexcept { requestedThresholdDb = newThresholdDb; }
        void setReleaseMode (ReleaseMode newMode) noexcept { releaseMode = newMode; }

        // Re-seeds the detector/state machine from the next sample's level
        // instead of ramping up from silence - called by the engine on the
        // gate's off -> on transition so engaging the gate mid-performance
        // neither clicks nor mutes the first ~10 ms of a sustained note.
        void requestSeed() noexcept { seedCountdown = seedGraceSamples; }

        // Advances the control path by one sample from `detectorInput` (the
        // stereo-linked max of |x| across channels, brief SS3.1) and returns
        // the linear gain to apply to every channel of that same sample.
        float processSample (float detectorInput) noexcept
        {
            // Threshold smoothing: a 20 ms linear ramp on the dB target, so
            // automating the threshold sweeps the gate rather than stepping
            // it (the state machine below consumes the smoothed value).
            const auto thresholdDelta = requestedThresholdDb - smoothedThresholdDb;
            if (std::abs (thresholdDelta) <= thresholdStepDb)
                smoothedThresholdDb = requestedThresholdDb;
            else
                smoothedThresholdDb += thresholdDelta > 0.0 ? thresholdStepDb : -thresholdStepDb;

            const auto filtered = sidechainLowPass.process (sidechainHighPass.process (static_cast<double> (detectorInput)));

            detector.process (filtered);

            const auto envelopeDb = detector.getLevelDb();

            // NLC anti-chatter smoother: the effective time constant
            // collapses towards tau_fast as |delta| grows past the knee, so a
            // real note attack is tracked immediately while sub-knee detector
            // ripple is smoothed away.
            const auto delta = envelopeDb - nlcDb;
            const auto knee = delta / nlcKneeDb;
            const auto blend = std::exp (-(knee * knee)); // 1 for tiny steps, ->0 for big ones
            const auto coefficient = nlcFastCoefficient + (nlcSlowCoefficient - nlcFastCoefficient) * blend;
            nlcDb += coefficient * delta;

            updateReleaseSlope (envelopeDb);

            // State machine with hysteresis + retriggering hold.
            const auto openThresholdDb = smoothedThresholdDb;
            const auto closeThresholdDb = smoothedThresholdDb - hysteresisDb;

            const auto wasOpen = gateOpen;

            if (seedCountdown > 0)
            {
                // Grace window: hold the gate open while the detector and
                // the NLC smoother converge (see seedGraceSeconds). The
                // envelopes above keep running, so the state machine takes
                // over from a fully-settled detector rather than from
                // silence.
                --seedCountdown;
                gateOpen = true;
                holdCounter = holdSamples;
                gainDb += attackCoefficient * (0.0 - gainDb);
                currentGain = static_cast<float> (std::exp (gainDb * 0.11512925464970229));
                return currentGain;
            }

            if (nlcDb >= openThresholdDb)
            {
                gateOpen = true;
                holdCounter = holdSamples;
                fastReleaseLatched = false; // a fresh note re-arms the TVP dump
            }
            else if (nlcDb <= closeThresholdDb)
            {
                gateOpen = false;
            }

            if (wasOpen != gateOpen)
                ++stateTransitions;

            bool targetOpen = gateOpen;

            if (! gateOpen && holdCounter > 0)
            {
                --holdCounter;
                targetOpen = true;
            }

            if (targetOpen)
            {
                // Opening: one-pole on the rising gain branch (tau 0.1 ms).
                gainDb += attackCoefficient * (0.0 - gainDb);
            }
            else
            {
                // Closing: dB-LINEAR ramp down to the range floor.
                gainDb = std::max (gainDb - currentSlopeDbPerSecond / sampleRate, -rangeDb);
            }

            // 10^(g/20) without a pow() call.
            currentGain = static_cast<float> (std::exp (gainDb * 0.11512925464970229));
            return currentGain;
        }

        //======================================================================
        // Telemetry (tests only - tests/NoiseGateTests.cpp needs to separate
        // the detector's own settling time from the hold/release timings it
        // asserts). Reading these is free and they are never consumed by the
        // audio path itself.
        double getGainDb() const noexcept { return gainDb; }
        float getGain() const noexcept { return currentGain; }
        double getDetectorDb() const noexcept { return nlcDb; }
        double getSmoothedThresholdDb() const noexcept { return smoothedThresholdDb; }
        double getReleaseSlopeDbPerSecond() const noexcept { return currentSlopeDbPerSecond; }
        bool isOpen() const noexcept { return gateOpen; }
        int getStateTransitionCount() const noexcept { return stateTransitions; }
        void clearStateTransitionCount() noexcept { stateTransitions = 0; }

    private:
        // Chooses the dB/s closing slope for this sample. Fast/Slow are
        // fixed; Auto runs the TVP dual-envelope discriminator.
        void updateReleaseSlope (double envelopeDb) noexcept
        {
            if (releaseMode == ReleaseMode::fast)
            {
                currentSlopeDbPerSecond = fastSlopeDbPerSecond;
                return;
            }

            if (releaseMode == ReleaseMode::slow)
            {
                currentSlopeDbPerSecond = slowSlopeDbPerSecond;
                return;
            }

            const auto fastDb = tvpFast.process (envelopeDb);

            const auto previousSlowDb = slowEnvelopeDb;
            slowEnvelopeDb = std::max (fastDb, slowEnvelopeDb - tvpSlowSlopeDbPerSecond / sampleRate);

            // Decay estimate: the slow envelope's own downward rate, itself
            // smoothed over 50 ms so a single ripple period cannot spike it.
            const auto instantaneousDecay = (previousSlowDb - slowEnvelopeDb) * sampleRate;
            decayEstimateDbPerSecond = instantaneousDecay
                                       + decayEstimateCoefficient * (decayEstimateDbPerSecond - instantaneousDecay);

            if ((slowEnvelopeDb - fastDb) > tvpWindowDb)
            {
                slowEnvelopeDb = fastDb; // dump
                fastReleaseLatched = true;
            }

            currentSlopeDbPerSecond = fastReleaseLatched
                                          ? tvpFastSlopeDbPerSecond
                                          : juce::jlimit (0.0, tvpTrackMaxDbPerSecond, decayEstimateDbPerSecond)
                                                + tvpTrackMarginDbPerSecond;
        }

        double sampleRate = 48000.0;

        TptSvf sidechainHighPass;
        TptSvf sidechainLowPass;
        MeanSquareFollower detector;
        BranchingOnePole tvpFast;

        double nlcFastCoefficient = 0.0;
        double nlcSlowCoefficient = 0.0;
        double attackCoefficient = 0.0;
        double decayEstimateCoefficient = 0.0;
        double thresholdStepDb = 0.0;

        double nlcDb = -160.0;
        double slowEnvelopeDb = -160.0;
        double decayEstimateDbPerSecond = 0.0;
        double currentSlopeDbPerSecond = fastSlopeDbPerSecond;

        double requestedThresholdDb = -50.0;
        double smoothedThresholdDb = -50.0;
        ReleaseMode releaseMode = ReleaseMode::automatic;

        double gainDb = 0.0;
        float currentGain = 1.0f;

        bool gateOpen = true;
        bool fastReleaseLatched = false;
        int holdSamples = 0;
        int holdCounter = 0;
        int seedGraceSamples = 0;
        int seedCountdown = 0;
        int stateTransitions = 0;
    };
}
