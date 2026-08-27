#include "OvertureEngine.h"

#include <cmath>

#include "KneeSoftening.h"
#include "RealtimeCoefficients.h"

namespace
{
    // Keeps a requested filter frequency safely below Nyquist regardless of
    // host sample rate, so juce::dsp::IIR::Coefficients::makeHighPass/
    // makeLowPass never receives an out-of-range value (which would produce
    // invalid/NaN coefficients). Tight (max 400 Hz) and the Bite/Bite Tilt
    // shelf corners (700 Hz/3 kHz fixed) are all far below Nyquist at any
    // realistic audio sample rate, but this guard costs nothing and removes
    // the assumption entirely.
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

OvertureEngine::OvertureEngine() = default;

void OvertureEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    // One second at the prepared rate - see restFlushDwellSamples.
    restFlushDwellSamples = static_cast<juce::int64> (std::llround (spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0));
    silentInputStreak = 0;
    restFlushed = false;

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

    // The Bite shelf runs on the up-sampled block (see processChunk()), so
    // it needs its own ProcessSpec at the oversampled rate/block size, not
    // the base spec above.
    oversampledSampleRate = sampleRate * static_cast<double> (oversampler->getOversamplingFactor());
    juce::dsp::ProcessSpec oversampledSpec;
    oversampledSpec.sampleRate = oversampledSampleRate;
    oversampledSpec.maximumBlockSize = spec.maximumBlockSize * static_cast<juce::uint32> (oversampler->getOversamplingFactor());
    oversampledSpec.numChannels = spec.numChannels;
    biteShelf.prepare (oversampledSpec);

    biteTiltShelf.prepare (spec);
    outputLevel.setRampDurationSeconds (smoothingTimeSeconds);
    outputLevel.prepare (spec);

    dryWetMixer.prepare (spec);

    // v0.3.0 stages. The gate runs at the BASE rate (it is a multiplicative
    // gain, not a waveshaper - no oversampling, no ADAA, zero latency); the
    // Feedback circuit solver, the ADAA wrapper and the Knee Response
    // envelope all run at the OVERSAMPLED rate; the DC blocker runs at the
    // base rate again, post-downsample.
    noiseGate.prepare (sampleRate, static_cast<int> (spec.numChannels));
    noiseGate.setThresholdDb (lastGateThresholdDb);
    noiseGate.setReleaseMode (gateReleaseMode);

    feedbackStage.prepare (oversampledSampleRate, static_cast<int> (spec.numChannels));
    feedbackStage.setDriveDb (lastDriveDb);
    feedbackStage.setAsymmetry01 (static_cast<double> (lastAsymmetryAmountPercent) * 0.01);
    feedbackStage.snapSmoothingToTarget();

    adaaWaveshaper.prepare (static_cast<int> (spec.numChannels));
    dcBlocker.prepare (sampleRate, static_cast<int> (spec.numChannels));
    kneeEnvelope.prepare (static_cast<int> (spec.numChannels), kneeEnvelopeReleaseSeconds, oversampledSampleRate);

    // Force the first sub-block after every prepare() to recompute every
    // filter's coefficients (see the members' docs in OvertureEngine.h):
    // prepare()'s priming below uses the allocating IIR::Coefficients
    // factory, whose result can differ by an ULP from the non-allocating
    // ArrayCoefficients path processSubBlock() uses, and v0.2.0 always
    // landed on the latter from the very first block.
    lastAppliedTightHz = -1.0f;
    lastAppliedBiteAmountPercent = -1.0f;
    lastAppliedBiteTiltPercent = -1.0e9f;

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
    mixSmoothed.reset (sampleRate, smoothingTimeSeconds);
    mixSmoothed.setCurrentAndTargetValue (lastMixProportion);
    biteAmountSmoothed.reset (sampleRate, smoothingTimeSeconds);
    biteAmountSmoothed.setCurrentAndTargetValue (lastBiteAmountPercent);
    kneeSoftenSmoothed.reset (sampleRate, smoothingTimeSeconds);
    kneeSoftenSmoothed.setCurrentAndTargetValue (lastKneeSoftenPercent);
    asymmetryAmountSmoothed.reset (sampleRate, smoothingTimeSeconds);
    asymmetryAmountSmoothed.setCurrentAndTargetValue (lastAsymmetryAmountPercent);
    biteTiltSmoothed.reset (sampleRate, smoothingTimeSeconds);
    biteTiltSmoothed.setCurrentAndTargetValue (lastBiteTiltPercent);

    reset();

    // Prime the filter coefficients immediately so the very first
    // process() call runs with correct, non-default coefficients rather
    // than an identity/uninitialised state. The allocating
    // IIR::Coefficients::make*() calls are fine here (prepare() is never
    // called from the audio thread) - process()/processChunk() instead uses
    // the non-allocating ArrayCoefficients + ovtr::applyBiquadCoefficients
    // path (see RealtimeCoefficients.h), which requires `state` to already
    // hold a validly-shaped (2nd-order) Coefficients object, which is what
    // this priming step guarantees from the first block onward.
    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, clampBelowNyquist (lastTightHz, sampleRate), shelfQ);
    *biteShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        oversampledSampleRate, biteShelfCornerHz, shelfQ, 1.0f); // unity/flat - see processChunk()'s skip-when-0 gate
    *biteTiltShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, biteTiltCornerHz, shelfQ, 1.0f); // unity/flat - see processChunk()'s skip-when-0 gate
}

void OvertureEngine::reset()
{
    tightHighPass.reset();
    driveGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    biteShelf.reset();
    biteTiltShelf.reset();
    outputLevel.reset();
    dryWetMixer.reset();

    noiseGate.reset();
    feedbackStage.reset();
    adaaWaveshaper.reset();
    dcBlocker.reset();
    kneeEnvelope.reset (0.0);
}

//==============================================================================
void OvertureEngine::setGateEnabled (bool shouldBeEnabled) noexcept
{
    gateEnabled = shouldBeEnabled;
}

void OvertureEngine::setGateThresholdDb (float newThresholdDb) noexcept
{
    lastGateThresholdDb = newThresholdDb;
    noiseGate.setThresholdDb (static_cast<double> (newThresholdDb));
}

void OvertureEngine::setGateReleaseMode (basilica::dsp::NoiseGate::ReleaseMode newMode) noexcept
{
    gateReleaseMode = newMode;
    noiseGate.setReleaseMode (newMode);
}

void OvertureEngine::setKneeResponseMode (KneeResponseMode newMode) noexcept
{
    if (newMode == kneeResponseMode)
        return;

    // Pre-seed the envelope from the outgoing mode's intensity so switching
    // modes mid-signal does not step the knee blend (brief SS4).
    if (newMode == KneeResponseMode::signal)
        kneeEnvelope.reset (static_cast<double> (
            KneeSoftening::intensityFromDriveDb (lastDriveDb, driveIntensityReferenceDb)));

    kneeResponseMode = newMode;
}

void OvertureEngine::setClipQualityMode (ClipQualityMode newMode) noexcept
{
    clipQualityMode = newMode;
}

void OvertureEngine::setParameterUpdateSubBlockSize (int newSubBlockSize) noexcept
{
    subBlockSize = static_cast<size_t> (juce::jmax (1, newSubBlockSize));
}

void OvertureEngine::setDriveDb (float newDriveDb)
{
    lastDriveDb = newDriveDb;
    driveGain.setGainDecibels (newDriveDb);

    // The Feedback voicing consumes Drive as the circuit's feedback
    // resistance R2 = 51k + D*500k rather than as a pre-clipper gain (see
    // src/dsp/FeedbackClipperStage.h); processSubBlock() forces driveGain
    // itself to 0 dB while that voicing is selected.
    feedbackStage.setDriveDb (static_cast<double> (newDriveDb));
}

void OvertureEngine::setTightFrequencyHz (float newFrequencyHz)
{
    lastTightHz = newFrequencyHz;
    tightFrequencySmoothed.setTargetValue (newFrequencyHz);
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

void OvertureEngine::setBiteAmountPercent (float newBiteAmountPercent)
{
    lastBiteAmountPercent = newBiteAmountPercent;
    biteAmountSmoothed.setTargetValue (newBiteAmountPercent);
}

void OvertureEngine::setKneeSoftenPercent (float newKneeSoftenPercent)
{
    lastKneeSoftenPercent = newKneeSoftenPercent;
    kneeSoftenSmoothed.setTargetValue (newKneeSoftenPercent);
}

void OvertureEngine::setAsymmetryAmountPercent (float newAsymmetryAmountPercent)
{
    lastAsymmetryAmountPercent = newAsymmetryAmountPercent;
    asymmetryAmountSmoothed.setTargetValue (newAsymmetryAmountPercent);
}

void OvertureEngine::setBiteTiltPercent (float newBiteTiltPercent)
{
    lastBiteTiltPercent = newBiteTiltPercent;
    biteTiltSmoothed.setTargetValue (newBiteTiltPercent);
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

    // Exact-zero rest guarantee (fleet audit class 2b, issue #35): with
    // JUCE_DSP_ENABLE_SNAP_TO_ZERO=0 the juce_dsp filters (Tight HPF, the
    // oversampler's own IIR stages, the shelves) no longer snap their
    // state to zero once per block, and a recursion can rest on a
    // rounding/FTZ fixed point instead of decaying (measured here at
    // ~7e-37 resting output; the same class Miserere#46 and Firmament#35
    // fixed). The silence-gated flush below restores what the library pass
    // provided, at engine scope and off the hot path - the input scan
    // short-circuits at the first non-zero sample, so it costs nothing
    // while programme material plays.
    const auto numChannels = block.getNumChannels();
    bool inputIsSilent = true;

    for (size_t channel = 0; channel < numChannels && inputIsSilent; ++channel)
    {
        const auto* data = block.getChannelPointer (channel);

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            if (data[sample] != 0.0f)
            {
                inputIsSilent = false;
                break;
            }
        }
    }

    if (inputIsSilent)
        silentInputStreak += static_cast<juce::int64> (numSamples);
    else
    {
        silentInputStreak = 0;
        restFlushed = false;
    }

    for (size_t offset = 0; offset < numSamples; offset += chunkLimit)
    {
        const auto chunkLength = juce::jmin (chunkLimit, numSamples - offset);
        auto chunk = block.getSubBlock (offset, chunkLength);
        processChunk (chunk);
    }

    // The dwell guarantees no in-flight audio (the oversampler's and
    // DryWetMixer's latency is a few dozen samples, orders of magnitude
    // below one second) can be swallowed: only after a full second of
    // contiguous silent input AND a residue already below the library's
    // own snap threshold is the engine considered drained. One-shot per
    // silent stretch; reset() touches no parameter smoothers, so wake-up
    // behaviour is unchanged.
    if (inputIsSilent && ! restFlushed && silentInputStreak >= restFlushDwellSamples)
    {
        auto residue = 0.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto* data = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < numSamples; ++sample)
                residue = juce::jmax (residue, std::abs (data[sample]));
        }

        if (residue < restFlushThreshold)
        {
            reset();
            block.clear();
            restFlushed = true;
        }
    }
}

void OvertureEngine::processChunk (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // v0.3.0 sub-block loop: every parameter smoother, coefficient update
    // and mode dispatch happens at this cadence (32 base-rate samples by
    // default => ~1.5 kHz at 48 kHz, up from v0.2.0's ~86 Hz block rate).
    // The filters, the oversampler and the DryWetMixer are all block-size
    // invariant IIR/delay structures, so splitting the work does not change
    // the output for static parameters - it only stops automation from
    // stepping the coefficients once per host block.
    const auto step = subBlockSize > 0 ? subBlockSize : numSamples;

    for (size_t offset = 0; offset < numSamples; offset += step)
    {
        const auto length = juce::jmin (step, numSamples - offset);
        auto subBlock = block.getSubBlock (offset, length);
        processSubBlock (subBlock);
    }
}

void OvertureEngine::processSubBlock (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();
    const auto numChannels = block.getNumChannels();

    // Coefficient recomputation involves trig calls (tan/cos), so filter
    // frequencies/percentages are smoothed and re-derived once per sub-block
    // rather than per sample - a standard real-time-safe compromise for IIR
    // filters, whose coefficients aren't cheap to interpolate directly.
    // Drive/Level still ramp sample-accurately via juce::dsp::Gain's
    // internal SmoothedValue, the gate's control path is per-sample
    // regardless (see src/dsp/NoiseGate.h), and Mix is re-applied every
    // sub-block below.
    const auto tightHz = clampBelowNyquist (tightFrequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto wetMix = mixSmoothed.skip (static_cast<int> (numSamples));
    const auto biteAmountPercent = biteAmountSmoothed.skip (static_cast<int> (numSamples));
    const auto kneeSoftenPercent = kneeSoftenSmoothed.skip (static_cast<int> (numSamples));
    const auto asymmetryAmountPercent = asymmetryAmountSmoothed.skip (static_cast<int> (numSamples));
    const auto biteTiltPercent = biteTiltSmoothed.skip (static_cast<int> (numSamples));

    const auto isFeedbackVoicing = currentVoicing == ClipperVoicing::feedback;
    const auto useAdaa = clipQualityMode == ClipQualityMode::enhanced && ! isFeedbackVoicing;
    const auto useDcBlocker = isFeedbackVoicing || clipQualityMode == ClipQualityMode::enhanced;

    // Non-allocating coefficient update (issue #12): ArrayCoefficients::
    // makeHighPass computes into a stack std::array, which
    // applyBiquadCoefficients then writes into the already-allocated
    // Coefficients storage primed by prepare() - no heap traffic on the
    // audio thread. v0.3.0 additionally skips the recompute entirely while
    // the smoothed value has not moved (epsilon compare), which is what
    // keeps a settled parameter's coefficients - and therefore the output -
    // bit-identical to v0.2.0 despite the 16x higher update cadence.
    if (std::abs (tightHz - lastAppliedTightHz) > coefficientEpsilonHz)
    {
        ovtr::applyBiquadCoefficients (*tightHighPass.state,
            juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, tightHz, shelfQ));
        lastAppliedTightHz = tightHz;
    }

    dryWetMixer.setWetMixProportion (wetMix);

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Capture the pre-processing signal as "dry" before any wet-path
    // filtering touches `block`. DryWetMixer internally delays this by
    // getLatencySamples() (set via setWetLatency in prepare()) so it stays
    // time-aligned with the oversampled wet path below.
    //
    // This happens BEFORE the gate, deliberately: the dry path is ungated
    // (the gate belongs to the wet pedal chain, exactly as an outboard gate
    // in front of an amp would - documented in docs/manual.md), so a Mix
    // below 100% still lets the untreated input through.
    dryWetMixer.pushDrySamples (block);

    // Built-in noise gate (v0.3.0). Detector taps the plugin input, before
    // Tight and before Drive; the resulting gain is applied to the wet path
    // input so gated noise never reaches the clipper at all. Per-sample
    // control path - a block-rate gate chatters.
    if (gateEnabled)
    {
        if (! lastGateEnabled)
            noiseGate.requestSeed();

        for (size_t sample = 0; sample < numSamples; ++sample)
        {
            // Stereo linked: one detector fed by the max of |x| across
            // channels, one gain applied to all of them, so the image never
            // wanders as the gate opens and closes.
            float detectorInput = 0.0f;

            for (size_t channel = 0; channel < numChannels; ++channel)
                detectorInput = juce::jmax (detectorInput, std::abs (block.getChannelPointer (channel)[sample]));

            const auto gateGain = noiseGate.processSample (detectorInput);

            for (size_t channel = 0; channel < numChannels; ++channel)
                block.getChannelPointer (channel)[sample] *= gateGain;
        }
    }

    lastGateEnabled = gateEnabled;

    // The Feedback voicing computes its own 21.5-41.4 dB in-band loop gain
    // from the circuit, so the plain pre-clipper Drive gain is forced flat
    // for it (brief SS3.2). juce::dsp::Gain::setGainDecibels early-returns
    // when the target is unchanged, so for the three legacy voicings this
    // re-application of lastDriveDb is a no-op and leaves the ramp - and the
    // output - exactly as v0.2.0 left it.
    driveGain.setGainDecibels (isFeedbackVoicing ? 0.0f : lastDriveDb);

    tightHighPass.process (context);
    driveGain.process (context);

    auto oversampledBlock = oversampler->processSamplesUp (block);
    const auto numOversampledSamples = oversampledBlock.getNumSamples();
    const auto numOversampledChannels = oversampledBlock.getNumChannels();

    // Asymmetry: asymmetryAmount (0-100%) maps linearly to the Asymmetric
    // voicing's bias `a` in 0.0-asymmetryMaxBias. Only the Asymmetric
    // voicing consumes this (see ClipperVoicings::processSample) - Soft
    // Symmetric/Hard Clip ignore it, exactly as in v0.1. The Feedback
    // voicing consumes the raw 0-1 proportion instead, as a morph of the
    // diode law itself (see below).
    const auto asymmetryA = (asymmetryAmountPercent * 0.01f) * asymmetryMaxBias;

    if (isFeedbackVoicing)
    {
        // Circuit-solved voicing: no Bite shelf (the circuit's own 720 Hz
        // pre-emphasis IS the bite mechanism) and no Knee Soften blend (its
        // knee is physical) - both controls are inert here, which
        // tests/EngineTests.cpp T-E2 asserts bit-for-bit rather than
        // documents.
        feedbackStage.setAsymmetry01 (static_cast<double> (asymmetryAmountPercent) * 0.01);

        // Sample-major: the solver advances its shared, resistance-domain
        // Drive smoother once per sample frame (on channel 0), so the
        // channels have to move through it in lockstep.
        for (size_t sample = 0; sample < numOversampledSamples; ++sample)
            for (size_t channel = 0; channel < numOversampledChannels; ++channel)
            {
                auto* channelData = oversampledBlock.getChannelPointer (channel);
                channelData[sample] = feedbackStage.processSample (static_cast<int> (channel), channelData[sample]);
            }
    }
    else
    {
        // Bite: frequency-dependent gain INSIDE the drive-to-clipper path
        // (see docs/design-brief.md SS"bite_amount" and the class-level docs
        // in OvertureEngine.h) - a low-shelf run on the up-sampled block,
        // immediately before the nonlinearity, that reduces the drive fed to
        // the clipper below biteShelfCornerHz, scaled by biteAmountPercent.
        // Skipped entirely (not just given unity-gain coefficients) when
        // biteAmountPercent is exactly 0, so bite_amount = 0 leaves the
        // oversampled signal reaching the clipper bit-identical to v0.1's
        // plain drive-gain-then-clip path.
        if (biteAmountPercent > 0.0f)
        {
            if (std::abs (biteAmountPercent - lastAppliedBiteAmountPercent) > coefficientEpsilonPercent)
            {
                const auto biteCutDb = -(biteAmountPercent * 0.01f) * biteShelfMaxCutDb;
                const auto biteGainFactor = juce::Decibels::decibelsToGain (biteCutDb);

                ovtr::applyBiquadCoefficients (*biteShelf.state,
                    juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (oversampledSampleRate, biteShelfCornerHz, shelfQ, biteGainFactor));
                lastAppliedBiteAmountPercent = biteAmountPercent;
            }

            juce::dsp::ProcessContextReplacing<float> biteContext (oversampledBlock);
            biteShelf.process (biteContext);
        }

        // Knee Soften: drive-dependent knee-softening blend amount (see
        // src/dsp/KneeSoftening.h). In the legacy "Drive" knee response the
        // intensity is the open-loop lastDriveDb/40 proxy, constant across
        // the sub-block and therefore bit-identical to v0.2.0's once-per-
        // chunk computation; in "Signal" mode it is a per-sample, per-channel
        // peak envelope of the actual oversampled clipper input.
        const auto kneeSoften01 = kneeSoftenPercent * 0.01f;
        const auto driveProxyIntensity01 = KneeSoftening::intensityFromDriveDb (lastDriveDb, driveIntensityReferenceDb);
        const auto useSignalKnee = kneeResponseMode == KneeResponseMode::signal;

        // currentVoicing does not change mid-block (set at most once per
        // process() call from the processor's atomic parameter read), so the
        // dispatch below is effectively free per sample.
        for (size_t channel = 0; channel < numOversampledChannels; ++channel)
        {
            auto* channelData = oversampledBlock.getChannelPointer (channel);

            for (size_t sample = 0; sample < numOversampledSamples; ++sample)
            {
                const auto u = channelData[sample];

                const auto intensity01 = useSignalKnee
                                             ? static_cast<float> (juce::jmin (1.0, kneeEnvelope.process (static_cast<int> (channel), std::abs (static_cast<double> (u)))))
                                             : driveProxyIntensity01;

                const auto raw = useAdaa
                                     ? adaaWaveshaper.process (static_cast<int> (channel), u, currentVoicing, asymmetryA)
                                     : ClipperVoicings::processSample (u, currentVoicing, asymmetryA);

                const auto kneeBlend01 = kneeSoften01 * intensity01;
                channelData[sample] = kneeBlend01 > 0.0f ? KneeSoftening::apply (raw, kneeBlend01) : raw;
            }
        }
    }

    oversampler->processSamplesDown (block);

    // DC blocker (5 Hz, post-downsample, pre-Bite-Tilt). Active for the
    // Feedback voicing (its asymmetric diode law produces programme-
    // dependent DC) and for Enhanced clip quality; the Classic legacy path
    // keeps its existing DC-bearing output bit-identical.
    if (useDcBlocker)
    {
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* channelData = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < numSamples; ++sample)
                channelData[sample] = dcBlocker.processSample (static_cast<int> (channel), channelData[sample]);
        }
    }

    // Bite Tilt: post-clip bidirectional shelf (replaces v0.1's cut-only
    // Tone LPF - see docs/design-brief.md's "Bite" section and the
    // class-level docs in OvertureEngine.h). Skipped entirely when
    // biteTiltPercent is exactly 0 (the default, flat position), so it is a
    // true no-op rather than a unity-gain filter call - the
    // bidirectionality guarantee tests/EngineTests.cpp verifies.
    if (biteTiltPercent != 0.0f)
    {
        if (std::abs (biteTiltPercent - lastAppliedBiteTiltPercent) > coefficientEpsilonPercent)
        {
            const auto tiltDb = (biteTiltPercent * 0.01f) * biteTiltMaxDb;
            // The explicit minus-infinity floor matters: at biteTilt =
            // -100% the requested shelf gain is exactly -100 dB, which is
            // ALSO juce::Decibels::decibelsToGain's default minus-infinity
            // threshold - without the second argument the gain factor
            // becomes exactly 0.0, makeHighShelf clamps that to its -300 dB
            // internal floor (A = 3.16e-8, JUCE 8.0.14
            // Decibels::gainWithLowerBound), and the resulting degenerate
            // biquad has a near-DC double pole (|z| ~ 0.99995): it swallows
            // the entire band by ~40 dB instead of shelving the top, and
            // its huge internal state random-walks on float rounding noise
            // at ~-40 dBFS for minutes of digital silence (caught by the
            // decaying-tail denormal test, issue #35). -200 dB is
            // unreachable by the +/-100 dB tilt range, so every legal
            // tiltDb now maps through the same pow() path bit-identically.
            const auto tiltGainFactor = juce::Decibels::decibelsToGain (tiltDb, -200.0f);

            ovtr::applyBiquadCoefficients (*biteTiltShelf.state,
                juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, biteTiltCornerHz, shelfQ, tiltGainFactor));
            lastAppliedBiteTiltPercent = biteTiltPercent;
        }

        biteTiltShelf.process (context);
    }

    outputLevel.process (context);

    dryWetMixer.mixWetSamples (block);
}
