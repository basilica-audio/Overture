#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// One-pole DC blocker, 5 Hz corner:
//
//   y[n] = x[n] - x[n-1] + R * y[n-1],     R = 1 - 2*pi*fc/fs
//
// Overture's Asymmetric voicing is deliberately biased (y = tanh(x+a) -
// tanh(a)), so a zero-mean input leaves it with a programme-dependent DC
// offset - harmless on its own, but it eats headroom downstream and shifts
// the operating point of whatever amp sim follows. The Feedback voicing's
// asymmetric diode law does the same thing for the same reason.
//
// v0.3.0 therefore runs this blocker post-downsample and pre-Bite-Tilt
// whenever ParamIDs::clipQuality is "Enhanced" OR the Feedback voicing is
// selected. The Classic path keeps its existing (DC-bearing) output
// bit-identical - the difference between the two paths is asserted in
// tests/AdaaTests.cpp (T-A3) rather than assumed.
//
// Header-only, per-channel state, real-time safe (prepare() is the only
// call that sizes anything).
namespace basilica::dsp
{
    class DcBlocker
    {
    public:
        static constexpr double cornerHz = 5.0;

        void prepare (double sampleRate, int numChannels)
        {
            const auto safeSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
            r = 1.0 - (2.0 * 3.14159265358979323846 * cornerHz / safeSampleRate);
            channels.assign (static_cast<size_t> (std::max (1, numChannels)), ChannelState {});
        }

        void reset() noexcept
        {
            for (auto& channel : channels)
                channel = ChannelState {};
        }

        int getNumChannels() const noexcept { return static_cast<int> (channels.size()); }

        float processSample (int channel, float x) noexcept
        {
            if (channel < 0 || channel >= static_cast<int> (channels.size()))
                return x;

            auto& state = channels[static_cast<size_t> (channel)];

            const auto input = static_cast<double> (x);
            const auto output = input - state.previousInput + r * state.previousOutput;

            state.previousInput = input;
            state.previousOutput = output;

            return static_cast<float> (output);
        }

    private:
        struct ChannelState
        {
            double previousInput = 0.0;
            double previousOutput = 0.0;
        };

        double r = 0.99934;
        std::vector<ChannelState> channels;
    };
}
