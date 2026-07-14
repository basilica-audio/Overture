#include "TightBoostEngine.h"

namespace
{
    // Keeps a requested filter frequency safely below Nyquist regardless of
    // host sample rate, so juce::dsp::IIR::Coefficients::makeHighPass/
    // makeLowPass never receives an out-of-range value (which would produce
    // invalid/NaN coefficients). Tight (max 400 Hz) and Tone (max 8 kHz) are
    // both far below Nyquist at any realistic audio sample rate, but this
    // guard costs nothing and removes the assumption entirely.
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

TightBoostEngine::TightBoostEngine() = default;

void TightBoostEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    tightHighPass.prepare (spec);
    driveGain.setRampDurationSeconds (smoothingTimeSeconds);
    driveGain.prepare (spec);

    // 4x oversampling (2^2), half-band polyphase IIR: much lower latency
    // than the equiripple FIR alternative for a given stopband quality,
    // which matters here because that latency is exactly what has to be
    // compensated on the dry path below. useIntegerLatency=true so the
    // reported latency (and therefore setLatencySamples()) is an exact
    // integer sample count rather than something we'd have to round.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        oversamplingFactorPow2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    oversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

    toneLowPass.prepare (spec);
    outputLevel.setRampDurationSeconds (smoothingTimeSeconds);
    outputLevel.prepare (spec);

    dryWetMixer.prepare (spec);

    latencySamples = static_cast<int> (std::round (oversampler->getLatencyInSamples()));
    dryWetMixer.setWetLatency (static_cast<float> (latencySamples));

    // juce::dsp::DryWetMixer defaults its internal mix to fully wet (1.0)
    // until told otherwise, and its own reset() (called from our reset()
    // below) snaps its internal dry/wet gain smoothers' *current* value to
    // whatever their *target* happens to be at that moment - it does not
    // know about lastMixProportion. Priming the real target here, before
    // reset() runs, means the mixer is already sitting at the correct dry/
    // wet balance for the very first process() call instead of ramping up
    // from "fully wet" over its internal 50ms default ramp.
    dryWetMixer.setWetMixProportion (lastMixProportion);

    // Re-seed the smoothers at the new sample rate, but pin current ==
    // target to whatever was last requested (defaulting to the
    // ParameterLayout defaults on first prepare) - otherwise the ramp would
    // sweep up from a default-constructed 0 Hz/0.0 on the very first block,
    // which is both audibly wrong and, for frequency, an invalid filter
    // cutoff of 0 Hz.
    tightFrequencySmoothed.reset (sampleRate, smoothingTimeSeconds);
    tightFrequencySmoothed.setCurrentAndTargetValue (lastTightHz);
    toneFrequencySmoothed.reset (sampleRate, smoothingTimeSeconds);
    toneFrequencySmoothed.setCurrentAndTargetValue (lastToneHz);
    mixSmoothed.reset (sampleRate, smoothingTimeSeconds);
    mixSmoothed.setCurrentAndTargetValue (lastMixProportion);

    reset();

    // Prime the filter coefficients immediately so the very first
    // process() call runs with correct, non-default coefficients rather
    // than an identity/uninitialised state.
    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, clampBelowNyquist (lastTightHz, sampleRate), filterQ);
    *toneLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
        sampleRate, clampBelowNyquist (lastToneHz, sampleRate), filterQ);
}

void TightBoostEngine::reset()
{
    tightHighPass.reset();
    driveGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    toneLowPass.reset();
    outputLevel.reset();
    dryWetMixer.reset();
}

void TightBoostEngine::setDriveDb (float newDriveDb)
{
    driveGain.setGainDecibels (newDriveDb);
}

void TightBoostEngine::setTightFrequencyHz (float newFrequencyHz)
{
    lastTightHz = newFrequencyHz;
    tightFrequencySmoothed.setTargetValue (newFrequencyHz);
}

void TightBoostEngine::setToneFrequencyHz (float newFrequencyHz)
{
    lastToneHz = newFrequencyHz;
    toneFrequencySmoothed.setTargetValue (newFrequencyHz);
}

void TightBoostEngine::setLevelDb (float newLevelDb)
{
    outputLevel.setGainDecibels (newLevelDb);
}

void TightBoostEngine::setMixProportion (float newProportion01)
{
    lastMixProportion = newProportion01;
    mixSmoothed.setTargetValue (newProportion01);
}

void TightBoostEngine::process (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // Coefficient recomputation involves trig calls (tan/cos), so filter
    // frequencies are smoothed and re-derived once per block rather than
    // per sample - a standard real-time-safe compromise for IIR filters,
    // whose coefficients aren't cheap to interpolate directly. Drive/Level
    // still ramp sample-accurately via juce::dsp::Gain's internal
    // SmoothedValue, and Mix is re-applied every block below.
    const auto tightHz = clampBelowNyquist (tightFrequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto toneHz = clampBelowNyquist (toneFrequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto wetMix = mixSmoothed.skip (static_cast<int> (numSamples));

    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, tightHz, filterQ);
    *toneLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, toneHz, filterQ);
    dryWetMixer.setWetMixProportion (wetMix);

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Capture the pre-processing signal as "dry" before any wet-path
    // filtering touches `block`. DryWetMixer internally delays this by
    // getLatencySamples() (set via setWetLatency in prepare()) so it stays
    // time-aligned with the oversampled wet path below.
    dryWetMixer.pushDrySamples (block);

    tightHighPass.process (context);
    driveGain.process (context);

    auto oversampledBlock = oversampler->processSamplesUp (block);

    for (size_t channel = 0; channel < oversampledBlock.getNumChannels(); ++channel)
    {
        auto* channelData = oversampledBlock.getChannelPointer (channel);

        for (size_t sample = 0; sample < oversampledBlock.getNumSamples(); ++sample)
            channelData[sample] = AsymSoftClipper::processSample (channelData[sample], clipperAsymmetry);
    }

    oversampler->processSamplesDown (block);

    toneLowPass.process (context);
    outputLevel.process (context);

    dryWetMixer.mixWetSamples (block);
}
