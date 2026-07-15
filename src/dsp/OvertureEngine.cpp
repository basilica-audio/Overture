#include "OvertureEngine.h"

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

OvertureEngine::OvertureEngine() = default;

void OvertureEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    preparedMaxBlockSize = static_cast<size_t> (spec.maximumBlockSize);

    tightHighPass.prepare (spec);
    driveGain.setRampDurationSeconds (smoothingTimeSeconds);
    driveGain.prepare (spec);

    // Oversampling factor is 2^oversamplingFactorPow2 (2x/4x/8x, selected
    // via setOversamplingFactorPow2() - defaults to 2, i.e. 4x, matching
    // the v0.1 fixed behaviour), half-band polyphase IIR: much lower
    // latency than the equiripple FIR alternative for a given stopband
    // quality, which matters here because that latency is exactly what has
    // to be compensated on the dry path below. useIntegerLatency=true so
    // the reported latency (and therefore setLatencySamples()) is an exact
    // integer sample count rather than something we'd have to round.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        static_cast<size_t> (oversamplingFactorPow2),
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    oversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

    toneLowPassStage1.prepare (spec);
    toneLowPassStage2.prepare (spec);
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
    // than an identity/uninitialised state. Tone is two cascaded 2nd-order
    // sections at the same cutoff but different Q (toneFilterQ1/Q2),
    // forming a single 4th-order Butterworth response.
    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, clampBelowNyquist (lastTightHz, sampleRate), tightFilterQ);
    const auto toneHzClamped = clampBelowNyquist (lastToneHz, sampleRate);
    *toneLowPassStage1.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, toneHzClamped, toneFilterQ1);
    *toneLowPassStage2.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, toneHzClamped, toneFilterQ2);
}

void OvertureEngine::reset()
{
    tightHighPass.reset();
    driveGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    toneLowPassStage1.reset();
    toneLowPassStage2.reset();
    outputLevel.reset();
    dryWetMixer.reset();
}

void OvertureEngine::setDriveDb (float newDriveDb)
{
    driveGain.setGainDecibels (newDriveDb);
}

void OvertureEngine::setTightFrequencyHz (float newFrequencyHz)
{
    lastTightHz = newFrequencyHz;
    tightFrequencySmoothed.setTargetValue (newFrequencyHz);
}

void OvertureEngine::setToneFrequencyHz (float newFrequencyHz)
{
    lastToneHz = newFrequencyHz;
    toneFrequencySmoothed.setTargetValue (newFrequencyHz);
}

void OvertureEngine::setLevelDb (float newLevelDb)
{
    outputLevel.setGainDecibels (newLevelDb);
}

void OvertureEngine::setMixProportion (float newProportion01)
{
    lastMixProportion = newProportion01;
    mixSmoothed.setTargetValue (newProportion01);
}

void OvertureEngine::setClipperVoicing (ClipperVoicing newVoicing) noexcept
{
    currentVoicing = newVoicing;
}

void OvertureEngine::setOversamplingFactorPow2 (int newFactorPow2) noexcept
{
    oversamplingFactorPow2 = juce::jlimit (1, 3, newFactorPow2);
}

void OvertureEngine::process (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // Defensive chunking against a host that violates its own
    // prepareToPlay()/processBlock() size contract (issue #13): the
    // oversampler's and DryWetMixer's internal buffers are both fixed to
    // preparedMaxBlockSize and only guarded by a jassert, which compiles
    // out in Release builds (JUCE 8.0.14 juce_Oversampling.cpp's
    // processSamplesUp/Down and juce_DryWetMixer.cpp's pushDrySamples/
    // mixWetSamples). Splitting into chunks of at most preparedMaxBlockSize
    // keeps every call into those internals within the size they were
    // prepared for, in both Debug and Release, without reallocating
    // anything here. In the overwhelmingly common case (numSamples <=
    // preparedMaxBlockSize) this loop runs exactly once over the full
    // block, identical to the pre-#13 behaviour.
    const auto chunkLimit = preparedMaxBlockSize > 0 ? preparedMaxBlockSize : numSamples;

    for (size_t offset = 0; offset < numSamples; offset += chunkLimit)
    {
        const auto chunkLength = juce::jmin (chunkLimit, numSamples - offset);
        auto chunk = block.getSubBlock (offset, chunkLength);
        processChunk (chunk);
    }
}

void OvertureEngine::processChunk (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();

    // Coefficient recomputation involves trig calls (tan/cos), so filter
    // frequencies are smoothed and re-derived once per chunk rather than
    // per sample - a standard real-time-safe compromise for IIR filters,
    // whose coefficients aren't cheap to interpolate directly. Drive/Level
    // still ramp sample-accurately via juce::dsp::Gain's internal
    // SmoothedValue, and Mix is re-applied every chunk below.
    const auto tightHz = clampBelowNyquist (tightFrequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto toneHz = clampBelowNyquist (toneFrequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto wetMix = mixSmoothed.skip (static_cast<int> (numSamples));

    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, tightHz, tightFilterQ);
    // Both tone sections share the same cutoff; only Q differs, per the
    // 4th-order Butterworth cascade design (see toneFilterQ1/Q2 docs).
    *toneLowPassStage1.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, toneHz, toneFilterQ1);
    *toneLowPassStage2.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, toneHz, toneFilterQ2);
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

    // currentVoicing does not change mid-block (set at most once per
    // process() call from the processor's atomic parameter read), so this
    // switch is effectively free per sample - see ClipperVoicings::processSample.
    for (size_t channel = 0; channel < oversampledBlock.getNumChannels(); ++channel)
    {
        auto* channelData = oversampledBlock.getChannelPointer (channel);

        for (size_t sample = 0; sample < oversampledBlock.getNumSamples(); ++sample)
            channelData[sample] = ClipperVoicings::processSample (channelData[sample], currentVoicing, clipperAsymmetry);
    }

    oversampler->processSamplesDown (block);

    toneLowPassStage1.process (context);
    toneLowPassStage2.process (context);
    outputLevel.process (context);

    dryWetMixer.mixWetSamples (block);
}
