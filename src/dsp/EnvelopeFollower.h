#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Header-only envelope followers shared by Overture's v0.3.0 noise gate
// (src/dsp/NoiseGate.h) and its signal-dependent Knee Soften response
// (ParamIDs::kneeResponse, see src/dsp/OvertureEngine.cpp).
//
// Everything here works in `double` internally: the gate's control path is
// a per-sample log-domain state machine whose dB-linear release ramp has to
// stay numerically exact over a 90 dB range, and a float mantissa is not
// generous enough to keep a 1000 dB/s ramp free of visible quantisation at
// 192 kHz. The audio path itself stays `float` - only the control signals
// derived from it are computed in double (the placement Giannoulis et al.
// (JAES 60(6), 2012, "Digital Dynamic Range Compressor Design") recommend
// for log-domain dynamics processors).
//
// All state is preallocated by prepare() and only ever mutated in the
// per-sample process functions, so nothing here allocates on the audio
// thread.
namespace basilica::dsp
{
    // Converts a one-pole time constant (seconds, the time to reach 1-1/e
    // of a step) into its per-sample smoothing coefficient. A tau of zero
    // (or below) yields 0.0, i.e. an instantaneous follower.
    inline double onePoleCoefficient (double tauSeconds, double sampleRate) noexcept
    {
        if (tauSeconds <= 0.0 || sampleRate <= 0.0)
            return 0.0;

        return std::exp (-1.0 / (tauSeconds * sampleRate));
    }

    // Branching ("attack/release") one-pole peak follower. Rising input is
    // tracked with `attackCoefficient`, falling input with
    // `releaseCoefficient` - the classic asymmetric detector. Used both on
    // the rectified oversampled clipper input (Knee Response = Signal) and
    // on log-domain levels inside the gate's TVP release estimator, so the
    // input is deliberately left un-rectified here: callers decide whether
    // they are feeding |x| or a dB value.
    class BranchingOnePole
    {
    public:
        void setCoefficients (double newAttackCoefficient, double newReleaseCoefficient) noexcept
        {
            attackCoefficient = newAttackCoefficient;
            releaseCoefficient = newReleaseCoefficient;
        }

        void reset (double initialValue = 0.0) noexcept { value = initialValue; }

        double getValue() const noexcept { return value; }

        double process (double input) noexcept
        {
            const auto coefficient = input > value ? attackCoefficient : releaseCoefficient;
            value = input + coefficient * (value - input);
            return value;
        }

    private:
        double attackCoefficient = 0.0;
        double releaseCoefficient = 0.0;
        double value = 0.0;
    };

    // Instant-attack / exponential-release peak follower:
    //   env[n] = max(|x[n]|, releaseCoefficient * env[n-1])
    // This is the follower ParamIDs::kneeResponse = "Signal" runs on the
    // oversampled clipper input (brief SS3.4): attack is instantaneous so a
    // transient immediately softens the knee, release is a 30 ms one-pole so
    // the knee does not chatter between plectrum strokes. One instance per
    // channel; see MultiChannelPeakFollower below.
    class InstantPeakFollower
    {
    public:
        void setReleaseCoefficient (double newReleaseCoefficient) noexcept
        {
            releaseCoefficient = newReleaseCoefficient;
        }

        void reset (double initialValue = 0.0) noexcept { value = initialValue; }

        double getValue() const noexcept { return value; }

        double process (double rectifiedInput) noexcept
        {
            value = std::max (rectifiedInput, releaseCoefficient * value);
            return value;
        }

    private:
        double releaseCoefficient = 0.0;
        double value = 0.0;
    };

    // A per-channel bank of InstantPeakFollower. prepare() is the only
    // allocating call (it sizes the bank); process()/reset() are real-time
    // safe.
    class MultiChannelPeakFollower
    {
    public:
        void prepare (int numChannels, double releaseTauSeconds, double sampleRate)
        {
            followers.resize (static_cast<size_t> (std::max (0, numChannels)));
            const auto coefficient = onePoleCoefficient (releaseTauSeconds, sampleRate);

            for (auto& follower : followers)
                follower.setReleaseCoefficient (coefficient);
        }

        void reset (double initialValue = 0.0) noexcept
        {
            for (auto& follower : followers)
                follower.reset (initialValue);
        }

        int getNumChannels() const noexcept { return static_cast<int> (followers.size()); }

        double process (int channel, double rectifiedInput) noexcept
        {
            if (channel < 0 || channel >= static_cast<int> (followers.size()))
                return rectifiedInput;

            return followers[static_cast<size_t> (channel)].process (rectifiedInput);
        }

    private:
        std::vector<InstantPeakFollower> followers;
    };

    // Exponentially-weighted mean-square (RMS-power) follower, the gate's
    // primary detector (brief SS3.1):
    //   ms[n] = a * ms[n-1] + (1 - a) * x^2[n]
    // The +1e-30 bias keeps the subsequent 10*log10() finite (and, on
    // hardware without flush-to-zero, keeps the state out of the denormal
    // range) during digital silence.
    class MeanSquareFollower
    {
    public:
        static constexpr double silenceBias = 1.0e-30;

        void setCoefficient (double newCoefficient) noexcept { coefficient = newCoefficient; }

        void reset (double initialMeanSquare = 0.0) noexcept { meanSquare = initialMeanSquare; }

        double getMeanSquare() const noexcept { return meanSquare; }

        double process (double input) noexcept
        {
            meanSquare = coefficient * meanSquare + (1.0 - coefficient) * (input * input);
            return meanSquare;
        }

        // 10*log10(ms + bias): the detector's output in dB relative to a
        // full-scale sine's mean square of 0.5 being -3 dB. Callers compare
        // this directly against the gate threshold parameter.
        double getLevelDb() const noexcept
        {
            return 10.0 * std::log10 (meanSquare + silenceBias);
        }

    private:
        double coefficient = 0.0;
        double meanSquare = 0.0;
    };
}
