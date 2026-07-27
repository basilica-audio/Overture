#pragma once

#include "ClipperVoicing.h"

#include <algorithm>
#include <cmath>
#include <vector>

// First-order antiderivative anti-aliasing (ADAA) wrappers around Overture's
// three legacy voicings - the "Enhanced" setting of ParamIDs::clipQuality.
//
// A memoryless waveshaper y = f(x) generates harmonics far above Nyquist,
// which oversampling can only push away, never remove. ADAA instead
// convolves the nonlinearity with a one-sample box kernel *analytically*:
//
//   y~(n) = (F(u(n)) - F(u(n-1))) / (u(n) - u(n-1)),      F' = f
//
// which is the exact average of f over the segment between two consecutive
// samples. Where the denominator collapses (a flat segment) the expression
// is replaced by the midpoint evaluation f((u(n)+u(n-1))/2) - the analytic
// limit - guarded by an input-scaled epsilon so the branch is taken on
// genuine flatness rather than on float noise.
//
// Method: Parker, Zavalishin & Le Bivic, "Reducing the aliasing of
// nonlinear waveshaping using continuous-time convolution" (DAFx-16);
// Bilbao, Esqueda, Parker & Valimaki's antiderivative formulation; the
// stateful higher-order variant (Holters & Zolzer, DAFx-19) is explicitly
// out of scope for v0.3.0.
//
// Two documented side effects, both benign at the oversampling factors this
// plugin runs at and neither reported as latency:
//   - a half-sample group delay AT THE OVERSAMPLED RATE (<= 1/(2*OS) sample
//     at base rate),
//   - a mild (1 + z^-1)/2 high-frequency droop, again at the oversampled
//     rate.
//
// This header WRAPS the voicing functions; it never edits them. The Classic
// path still calls ClipperVoicings::processSample() directly and stays
// bit-identical to v0.2.0 - see the blacklist in the v0.3.0 brief SS5.
//
// State is per channel and lives in the wrapper, so reset() must clear it
// whenever the engine resets.
namespace basilica::dsp
{
    namespace adaa
    {
        // ln(cosh(x)) with an overflow-safe large-|x| branch:
        //   ln(cosh x) = |x| + ln((1 + e^(-2|x|))/2) = |x| - ln2 + ln1p(e^(-2|x|))
        inline double logCosh (double x) noexcept
        {
            const auto ax = std::abs (x);

            if (ax < 12.0)
                return std::log (std::cosh (ax));

            return ax - 0.6931471805599453 + std::log1p (std::exp (-2.0 * ax));
        }

        // Antiderivative of the hard clamp to +/-1.
        inline double hardClipAntiderivative (double x) noexcept
        {
            const auto ax = std::abs (x);
            return ax <= 1.0 ? 0.5 * x * x : ax - 0.5;
        }

        // Antiderivative of tanh(x).
        inline double softSymmetricAntiderivative (double x) noexcept
        {
            return logCosh (x);
        }

        // Antiderivative of tanh(x + a) - tanh(a).
        inline double asymmetricAntiderivative (double x, double a) noexcept
        {
            return logCosh (x + a) - x * std::tanh (a);
        }

        inline double antiderivative (double x, ClipperVoicing voicing, double asymmetry) noexcept
        {
            switch (voicing)
            {
                case ClipperVoicing::softSymmetric: return softSymmetricAntiderivative (x);
                case ClipperVoicing::hardClip:      return hardClipAntiderivative (x);
                case ClipperVoicing::feedback:      return softSymmetricAntiderivative (x); // never reached - see AdaaWaveshaper::process
                case ClipperVoicing::asymmetric:
                default:                            return asymmetricAntiderivative (x, asymmetry);
            }
        }
    }

    class AdaaWaveshaper
    {
    public:
        void prepare (int numChannels)
        {
            channels.assign (static_cast<size_t> (std::max (1, numChannels)), ChannelState {});
        }

        void reset() noexcept
        {
            for (auto& channel : channels)
                channel = ChannelState {};
        }

        int getNumChannels() const noexcept { return static_cast<int> (channels.size()); }

        // First-order ADAA evaluation of `voicing` at `x` for one channel.
        // The Feedback voicing is a stateful circuit solver, not a
        // memoryless curve, and is never routed through here (see
        // OvertureEngine::processSubBlock()).
        float process (int channel, float x, ClipperVoicing voicing, float asymmetry) noexcept
        {
            if (channel < 0 || channel >= static_cast<int> (channels.size()))
                return ClipperVoicings::processSample (x, voicing, asymmetry);

            auto& state = channels[static_cast<size_t> (channel)];

            const auto u = static_cast<double> (x);
            const auto a = static_cast<double> (asymmetry);

            // The stored antiderivative is only valid for the (voicing,
            // asymmetry) pair it was computed with; a discrete voicing switch
            // or a moving Asymmetry control invalidates it, so recompute
            // rather than mixing two different F's across one difference.
            if (! state.primed || state.voicing != voicing || state.asymmetry != a)
            {
                state.previousInput = u;
                state.previousAntiderivative = adaa::antiderivative (u, voicing, a);
                state.voicing = voicing;
                state.asymmetry = a;
                state.primed = true;

                return ClipperVoicings::processSample (x, voicing, asymmetry);
            }

            const auto delta = u - state.previousInput;
            const auto epsilon = 1.0e-6 * std::max (1.0, std::abs (u));

            double y;

            if (std::abs (delta) > epsilon)
            {
                const auto antiderivativeNow = adaa::antiderivative (u, voicing, a);
                y = (antiderivativeNow - state.previousAntiderivative) / delta;
                state.previousAntiderivative = antiderivativeNow;
            }
            else
            {
                // Analytic limit of the difference quotient: evaluate the
                // nonlinearity itself at the segment midpoint.
                const auto midpoint = 0.5 * (u + state.previousInput);
                y = static_cast<double> (ClipperVoicings::processSample (static_cast<float> (midpoint), voicing, asymmetry));
                state.previousAntiderivative = adaa::antiderivative (u, voicing, a);
            }

            state.previousInput = u;

            return static_cast<float> (y);
        }

    private:
        struct ChannelState
        {
            double previousInput = 0.0;
            double previousAntiderivative = 0.0;
            double asymmetry = 0.0;
            ClipperVoicing voicing = ClipperVoicing::asymmetric;
            bool primed = false;
        };

        std::vector<ChannelState> channels;
    };
}
