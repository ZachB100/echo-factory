#include "DelayEngine.h"

namespace
{
    // Runaway: feedback from 110% (drive 0) to 160% (drive 1), ceiling from 0.5 down to 0.3.
    constexpr float runawayMinFeedback = 1.1f,  runawayFeedbackRange = 0.5f;
    constexpr float runawayMaxCeiling  = 0.5f,  runawayCeilingRange  = 0.2f;

    constexpr float dcBlockHz = 5.0f;
    constexpr float driftHz = 0.5f, driftIntervalSec = 0.4f;

    // Lo-Fi at full Wear: this many bits at this sample rate.
    constexpr float lofiMinBits = 8.0f, lofiMinRateHz = 8000.0f;

    // Shimmer's feed into the reverb at full.
    constexpr float maxShimmerSend = 0.6f;

    // What each Style does at full Wear. Studio Tape is the original character.
    struct StyleSpec
    {
        float minCeiling;   // saturator ceiling
        float darkestHz;    // one-pole darkening cutoff (20 kHz: none)
        float thinHz;       // one-pole low cut in the loop (0: none)
        float wowMs, wowHz, flutterMs, flutterHz;
        float driftMix;     // share of the wow that's random drift rather than the sine
        float lofi;         // 1: sample rate and bit depth fall with Wear
    };

    constexpr StyleSpec styles[DelayEngine::numStyles] {
        { 0.35f,  4500.0f,   0.0f, 1.0f,  0.7f, 0.03f,  7.3f, 0.4f, 0.0f },  // Studio Tape
        { 0.45f,  6500.0f,  90.0f, 0.7f,  0.9f, 0.12f,  9.5f, 0.3f, 0.0f },  // Tape Echo: brighter, more flutter, harder
        { 0.5f,   2800.0f,  60.0f, 0.25f, 0.4f, 0.05f, 11.0f, 0.5f, 0.0f },  // Bucket Brigade: dark and soft
        { 0.7f,  20000.0f,   0.0f, 0.0f,  0.7f, 0.0f,   7.3f, 0.0f, 1.0f },  // Lo-Fi Digital: fewer bits at a lower rate
        { 0.25f,  2600.0f, 450.0f, 0.0f,  0.7f, 0.0f,   7.3f, 0.0f, 0.0f },  // Telephone: narrow and clipped
    };

    // Taps
    constexpr double tapFadeSeconds = 0.01, tapGainSeconds = 0.02, multiTapFadeSeconds = 0.02;

    // Reverb line lengths in ms (left, right: different for a wide, decorrelated wash).
    constexpr float diffuserMs[2][DiffusionNetwork::numLines] {
        { 15.1f, 17.9f, 20.3f, 22.7f, 25.3f, 27.9f, 30.7f, 33.1f, 36.1f, 38.9f, 41.3f, 44.3f, 47.1f, 50.9f, 54.1f, 57.7f },
        { 16.3f, 18.7f, 21.1f, 23.9f, 26.3f, 29.1f, 31.9f, 34.3f, 37.1f, 39.7f, 42.7f, 45.1f, 48.3f, 51.7f, 55.3f, 58.9f },
    };
    // Pre-diffuser allpass lengths in ms (left, right).
    constexpr float preDiffuserMs[2][DiffusionNetwork::numPreStages] {
        { 17.3f, 29.1f, 43.7f, 61.3f },
        { 19.1f, 31.3f, 41.9f, 58.7f },
    };

    // Diffusion turns the delay into a reverb. With the square of the knob, the
    // delay's own feedback is handed over to the reverb's tail, whose length
    // grows from a small room to the decay the feedback would have given
    // (RT60 at low frequencies, at least 2 s, at most 20 s; the top end dies
    // away twice as fast). At full, the delay feeds the reverb once and Feedback
    // sets how long it rings, so there are no separate repeats left to hear.
    constexpr float minDiffusionRt60 = 0.3f, roomDiffusionRt60 = 2.0f, maxDiffusionRt60 = 20.0f;
    constexpr float diffusionHighRatio = 0.5f;

    float onePoleCoef (float hz, double sampleRate)
    {
        return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sampleRate);
    }
}

void DelayEngine::prepare (double newSampleRate, int maxBlockSize, int numChannels, float maxDelayMs, float maxWidthMs)
{
    sampleRate = newSampleRate;
    maxDelaySamples = (float) (maxDelayMs * 0.001 * sampleRate);

    // Output heads read up to four delay times back while reversing (the
    // segment is read backwards from one to three delay times, four for a tap
    // an octave up), plus the tapestop trail and the pitch window.
    maxReadSamples = 4.0f * maxDelaySamples + ReadHead::maxExtraSamples (sampleRate);
    delayLine.setMaximumDelayInSamples ((int) std::ceil (maxReadSamples) + 2);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) };
    delayLine.prepare (spec);

    maxWidthSamples = (float) (maxWidthMs * 0.001 * sampleRate);
    widthDelayLine.setMaximumDelayInSamples ((int) std::ceil (maxWidthSamples) + 2);
    widthDelayLine.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });

    lowCutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutFilter.prepare (spec);
    highCutFilter.prepare (spec);
    lowCutOutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutOutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutOutFilter.prepare (spec);
    highCutOutFilter.prepare (spec);
    lowCutTapFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCutTapFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutTapFilter.prepare (spec);
    highCutTapFilter.prepare (spec);

    head.prepare (sampleRate);

    for (size_t ch = 0; ch < 2; ++ch)
        diffusers[ch].prepare (diffuserMs[ch], preDiffuserMs[ch], sampleRate);

    for (auto& modulator : modulators)
        modulator.prepare (sampleRate);

    diffusion.reset (sampleRate, 0.05);
    shimmer.reset (sampleRate, 0.05);
    modInLoop.reset (sampleRate, 0.05);
    multiTapAmount.reset (sampleRate, multiTapFadeSeconds);
    setTapestopTimeMs (500.0f);
    setShimmerSemitones (shimmerSemitones);

    delaySmoothingSamples = -1; // forces the ramp length to be recalculated for this sample rate
    setDelaySmoothingMs (50.0f);
    lowCutHz.reset (sampleRate, 0.05);
    highCutHz.reset (sampleRate, 0.05);
    feedback.reset (sampleRate, 0.02);
    mix.reset (sampleRate, 0.02);
    freezeFadeSamples = -1;
    setFreezeFadeMs (20.0f);
    inputSend.reset (sampleRate, 0.01);
    wear.reset (sampleRate, 0.05);
    runawayAmount.reset (sampleRate, 0.08);
    runawayDrive.reset (sampleRate, 0.05);

    for (auto* smoothed : { &styleCeiling, &styleDarkCoef, &styleThinOn, &styleThinCoef, &styleWowMs, &styleFlutterMs, &styleDrift, &styleLofi })
        smoothed->reset (sampleRate, 0.05);

    dcCoef      = onePoleCoef (dcBlockHz, sampleRate);
    driftCoef   = onePoleCoef (driftHz, sampleRate);
    lofiMaxHold = juce::jmax (1.0f, (float) (sampleRate / lofiMinRateHz));
    setStyle (style); // the coefficients depend on the sample rate
    pingPongAmount.reset (sampleRate, 0.02);
    widthSamples.reset (sampleRate, 0.05);

    for (auto& voice : voices)
        prepareVoice (voice);

    reset();
}

void DelayEngine::reset()
{
    delayLine.reset();
    widthDelayLine.reset();
    lowCutFilter.reset();
    highCutFilter.reset();
    lowCutOutFilter.reset();
    highCutOutFilter.reset();
    lowCutHz.setCurrentAndTargetValue (lowCutHz.getTargetValue());
    highCutHz.setCurrentAndTargetValue (highCutHz.getTargetValue());
    lowCutFilter.setCutoffFrequency (lowCutHz.getCurrentValue());
    highCutFilter.setCutoffFrequency (highCutHz.getCurrentValue());
    lowCutOutFilter.setCutoffFrequency (lowCutHz.getCurrentValue());
    highCutOutFilter.setCutoffFrequency (highCutHz.getCurrentValue());
    delaySamples.setCurrentAndTargetValue (delaySamples.getTargetValue());
    feedback.setCurrentAndTargetValue (feedback.getTargetValue());
    mix.setCurrentAndTargetValue (mix.getTargetValue());
    freezeAmount.setCurrentAndTargetValue (freezeAmount.getTargetValue());
    pingPongAmount.setCurrentAndTargetValue (pingPongAmount.getTargetValue());
    inputSend.setCurrentAndTargetValue (inputSend.getTargetValue());
    wear.setCurrentAndTargetValue (wear.getTargetValue());
    runawayAmount.setCurrentAndTargetValue (runawayAmount.getTargetValue());
    runawayDrive.setCurrentAndTargetValue (runawayDrive.getTargetValue());

    for (auto* smoothed : { &styleCeiling, &styleDarkCoef, &styleThinOn, &styleThinCoef, &styleWowMs, &styleFlutterMs, &styleDrift, &styleLofi })
        smoothed->setCurrentAndTargetValue (smoothed->getTargetValue());

    for (int ch = 0; ch < 2; ++ch)
        dcState[ch] = thinState[ch] = darkState[ch] = held[ch] = holdPhase[ch] = 0.0f;

    wowPhase = flutterPhase = drift = driftTarget = 0.0f;
    driftCountdown = 0;

    diffusion.setCurrentAndTargetValue (diffusion.getTargetValue());
    shimmer.setCurrentAndTargetValue (shimmer.getTargetValue());
    head.reset();

    for (auto& network : diffusers)
        network.clear();

    diffusionActive = false;
    diffusionRt60 = 0.0f;     // forces the tail length to be recalculated
    diffusionShimmer = -1.0f;

    modInLoop.setCurrentAndTargetValue (modInLoop.getTargetValue());

    for (auto& modulator : modulators)
        modulator.reset();

    widthSamples.setCurrentAndTargetValue (widthSamples.getTargetValue());

    // Taps start again from silence the next time they're needed.
    multiTapAmount.setCurrentAndTargetValue (multiTapAmount.getTargetValue());
    tapsRunning = false;
}

void DelayEngine::setDelayMs (float ms)
{
    const auto samples = juce::jlimit (1.0f, maxDelaySamples, (float) (ms * 0.001 * sampleRate));
    delaySamples.setTargetValue (samples);
}

void DelayEngine::setDelaySmoothingMs (float ms)
{
    const auto numSamples = juce::roundToInt (juce::jmax (0.0f, ms) * 0.001 * sampleRate);

    if (numSamples == delaySmoothingSamples)
        return;

    delaySmoothingSamples = numSamples;

    // SmoothedValue::reset() snaps to the target, so carry on from wherever
    // the glide currently is, now at the new speed.
    const auto current = delaySamples.getCurrentValue();
    const auto target  = delaySamples.getTargetValue();
    delaySamples.reset (numSamples);
    delaySamples.setCurrentAndTargetValue (current);
    delaySamples.setTargetValue (target);
}

void DelayEngine::setFreezeFadeMs (float ms)
{
    const auto numSamples = juce::jmax (1, juce::roundToInt (ms * 0.001 * sampleRate));

    if (numSamples == freezeFadeSamples)
        return;

    freezeFadeSamples = numSamples;

    // As with the delay time, keep any fade in progress going from where it is.
    const auto current = freezeAmount.getCurrentValue();
    const auto target  = freezeAmount.getTargetValue();
    freezeAmount.reset (numSamples);
    freezeAmount.setCurrentAndTargetValue (current);
    freezeAmount.setTargetValue (target);
}

void DelayEngine::setStereoWidthMs (float ms)
{
    widthSamples.setTargetValue (juce::jlimit (0.0f, maxWidthSamples, (float) (ms * 0.001 * sampleRate)));
}

void DelayEngine::setFeedback (float amount01) { feedback.setTargetValue (juce::jlimit (0.0f, 0.95f, amount01)); }
void DelayEngine::setMix (float wet01)         { mix.setTargetValue (juce::jlimit (0.0f, 1.0f, wet01)); }

void DelayEngine::setStyle (int styleIndex)
{
    style = juce::jlimit (0, numStyles - 1, styleIndex);
    const auto& spec = styles[(size_t) style];

    styleCeiling.setTargetValue (spec.minCeiling);
    styleDarkCoef.setTargetValue (onePoleCoef (spec.darkestHz, sampleRate));
    styleThinOn.setTargetValue (spec.thinHz > 0.0f ? 1.0f : 0.0f);

    if (spec.thinHz > 0.0f)
        styleThinCoef.setTargetValue (onePoleCoef (spec.thinHz, sampleRate));

    styleWowMs.setTargetValue (spec.wowMs);
    styleFlutterMs.setTargetValue (spec.flutterMs);
    styleDrift.setTargetValue (spec.driftMix);
    styleLofi.setTargetValue (spec.lofi);

    // Rates change at once; the phases carry on, so nothing jumps.
    wowIncrement     = juce::MathConstants<float>::twoPi * spec.wowHz / (float) sampleRate;
    flutterIncrement = juce::MathConstants<float>::twoPi * spec.flutterHz / (float) sampleRate;
}

void DelayEngine::setShimmerSemitones (float semitones)
{
    shimmerSemitones = semitones;
    const auto ratio = std::exp2 (semitones / 12.0f);

    for (auto& network : diffusers)
        network.setShimmerRatio (ratio);
}

void DelayEngine::setModulationType (int type)
{
    for (auto& modulator : modulators)
        modulator.setType (static_cast<Modulator::Type> (juce::jlimit (0, Modulator::numTypes, type)));
}

void DelayEngine::setModulationRateHz (float hz)
{
    for (auto& modulator : modulators)
        modulator.setRateHz (hz);
}

void DelayEngine::setModulationDepth (float amount01)
{
    for (auto& modulator : modulators)
        modulator.setDepth (amount01);
}

void DelayEngine::setLowCutHz (float hz)
{
    lowCutOn = hz > 0.0f;

    if (lowCutOn)
        lowCutHz.setTargetValue (juce::jlimit (10.0f, (float) sampleRate * 0.45f, hz));
}

void DelayEngine::setHighCutHz (float hz)
{
    highCutOn = hz > 0.0f;

    if (highCutOn)
        highCutHz.setTargetValue (juce::jlimit (10.0f, (float) sampleRate * 0.45f, hz));
}

float DelayEngine::filter (int channel, float sample)
{
    // Filters always run so their state stays warm and switching them on doesn't click.
    const auto low  = lowCutFilter.processSample (channel, sample);
    const auto afterLow = lowCutOn ? low : sample;
    const auto high = highCutFilter.processSample (channel, afterLow);
    return highCutOn ? high : afterLow;
}

float DelayEngine::outputFilter (int channel, float sample)
{
    // Same chain as filter(), for the output head. While tapestop is idle it
    // sees exactly the same samples, so it produces exactly the same output.
    const auto low  = lowCutOutFilter.processSample (channel, sample);
    const auto afterLow = lowCutOn ? low : sample;
    const auto high = highCutOutFilter.processSample (channel, afterLow);
    return highCutOn ? high : afterLow;
}

void DelayEngine::setTapestopTimeMs (float ms)
{
    tapestopMs = ms;
    head.setTapestopTimeMs (ms);
}

float DelayEngine::readLine (int channel, float delay)
{
    return delayLine.popSample (channel, juce::jlimit (1.0f, maxReadSamples, delay), false);
}

float DelayEngine::reverberate (int channel, float echo)
{
    if (! diffusionActive)
        return echo;

    const auto reverb = diffusers[(size_t) channel].process (echo) * diffusionMakeup;
    return diffusionCos * echo + diffusionSin * reverb;
}

float DelayEngine::tape (int channel, float sample, const CharacterSettings& t)
{
    dcState[channel] += dcCoef * (sample - dcState[channel]);
    auto x = sample - t.amount * dcState[channel];

    // The thin styles lose their lows on every pass.
    thinState[channel] += t.thinCoef * (x - thinState[channel]);
    x -= t.thin * thinState[channel];

    const auto saturated = t.ceiling * std::tanh (x / t.ceiling);
    x += t.amount * (saturated - x);

    if (t.lofi > 0.0f)
    {
        // Lo-Fi: hold each sample for a while (a lower sample rate), rounded
        // towards zero to fewer bits. Towards zero, so a repeat below one step
        // dies instead of looping at that step forever.
        holdPhase[channel] += t.lofiHoldStep;

        if (holdPhase[channel] >= 1.0f)
        {
            holdPhase[channel] -= 1.0f;
            held[channel] = std::trunc (x * t.lofiSteps) / t.lofiSteps;
        }

        x += t.lofi * (held[channel] - x);
    }

    // Coefficient 1 passes the sample straight through.
    darkState[channel] += t.darkCoef * (x - darkState[channel]);
    return darkState[channel];
}

float DelayEngine::syncedTapMs (float beats) const
{
    // Swing: within each beat the first half stretches and the second half
    // shrinks, so at full Groove an off-beat eighth moves from halfway to two
    // thirds (the triplet), and the sixteenths around it move with it.
    const auto whole = std::floor (beats);
    auto part = beats - whole;
    const auto shift = groove / 6.0f;
    part = part < 0.5f ? part * (1.0f + 2.0f * shift)
                       : 0.5f + shift + (part - 0.5f) * (1.0f - 2.0f * shift);

    return (float) ((double) (whole + part) * 60000.0 / tapsBpm) + feelMs;
}

void DelayEngine::process (juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();
    const auto isStereo    = numChannels >= 2;
    const auto msToSamples = (float) (0.001 * sampleRate);

    updateVoices (isStereo);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frozen = freezeAmount.getNextValue();
        const auto wearNow = wear.getNextValue();
        const auto runaway = runawayAmount.getNextValue();
        const auto drive   = runawayDrive.getNextValue();

        // Runaway takes feedback past 100% and the saturator fully in, which keeps
        // the loop bounded at the ceiling. The saturator comes in twice as fast as
        // the feedback, so it's fully in before feedback passes 100% and loud input
        // can't overshoot on the way in.
        const auto diffusionNow = diffusion.getNextValue();
        const auto handover = diffusionNow * diffusionNow; // share of the sustain given to the reverb
        const auto feedbackNow = feedback.getNextValue();
        const auto fb = feedbackNow * (1.0f - runaway) * (1.0f - handover)
                      + runaway * (runawayMinFeedback + runawayFeedbackRange * drive);

        // The character stage, from Wear, Runaway and the (smoothed) style.
        const auto minCeiling = styleCeiling.getNextValue();
        const auto darkCoefMin = styleDarkCoef.getNextValue();
        const auto darken = juce::jmax (wearNow, 0.5f * runaway);

        CharacterSettings character;
        character.amount   = juce::jmax (wearNow, juce::jmin (1.0f, 2.0f * runaway));
        character.ceiling  = (1.0f - (1.0f - minCeiling) * wearNow) * (1.0f - runaway)
                         + runaway * (runawayMaxCeiling - runawayCeilingRange * drive);
        character.darkCoef = 1.0f - darken * (1.0f - darkCoefMin);
        character.thin     = wearNow * styleThinOn.getNextValue();
        character.thinCoef = styleThinCoef.getNextValue();
        character.lofi     = wearNow * styleLofi.getNextValue();
        character.lofiHoldStep = character.lofiSteps = 1.0f;

        if (character.lofi > 0.0f)
        {
            character.lofiHoldStep = 1.0f / (1.0f + character.lofi * (lofiMaxHold - 1.0f));
            character.lofiSteps = std::exp2 (16.0f - character.lofi * (16.0f - lofiMinBits));
        }

        const auto wet = mix.getNextValue();
        const auto send = inputSend.getNextValue();
        const auto pingPong = isStereo ? pingPongAmount.getNextValue() : 0.0f;
        const auto width = widthSamples.getNextValue();

        // Wow (slow, with random drift) and flutter (fast), faded out while frozen.
        if (--driftCountdown <= 0)
        {
            driftTarget = random.nextFloat() * 2.0f - 1.0f;
            driftCountdown = (int) (driftIntervalSec * sampleRate);
        }

        drift += driftCoef * (driftTarget - drift);
        wowPhase += wowIncrement;
        flutterPhase += flutterIncrement;
        if (wowPhase >= juce::MathConstants<float>::twoPi)     wowPhase -= juce::MathConstants<float>::twoPi;
        if (flutterPhase >= juce::MathConstants<float>::twoPi) flutterPhase -= juce::MathConstants<float>::twoPi;

        const auto driftMix = styleDrift.getNextValue();
        const auto wowDepth = wearNow * wearNow * styleWowMs.getNextValue() * msToSamples;
        const auto flutterDepth = wearNow * styleFlutterMs.getNextValue() * msToSamples;
        const auto modulation = (1.0f - frozen) * (wowDepth * ((1.0f - driftMix) * std::sin (wowPhase) + driftMix * drift)
                                                   + flutterDepth * std::sin (flutterPhase));

        // Whole-sample delay while frozen: repeated fractional interpolation
        // would slowly dull the looping audio.
        auto d = juce::jlimit (1.0f, maxDelaySamples, delaySamples.getNextValue() + modulation);
        if (frozen > 0.0f)
            d = std::round (d);

        if (lowCutHz.isSmoothing())
        {
            const auto hz = lowCutHz.getNextValue();
            lowCutFilter.setCutoffFrequency (hz);
            lowCutOutFilter.setCutoffFrequency (hz);
            lowCutTapFilter.setCutoffFrequency (hz);
        }

        if (highCutHz.isSmoothing())
        {
            const auto hz = highCutHz.getNextValue();
            highCutFilter.setCutoffFrequency (hz);
            highCutOutFilter.setCutoffFrequency (hz);
            highCutTapFilter.setCutoffFrequency (hz);
        }

        head.advance (d);

        // Diffusion blends the echoes into the reverb with an equal-power
        // crossfade. At 0 the reverb is out of the path; its tail is cleared.
        const auto shimmerNow = shimmer.getNextValue() * maxShimmerSend;

        if (diffusionNow > 0.0f)
        {
            if (! diffusionActive || ! juce::exactlyEqual (diffusionNow, diffusionSetting))
            {
                const auto theta = diffusionNow * juce::MathConstants<float>::halfPi;
                diffusionCos = std::cos (theta);
                diffusionSin = std::sin (theta);
                diffusionSetting = diffusionNow;
            }

            // The decay the delay's feedback gives: 60 dB at -20 log10(fb) dB per period.
            const auto delaySeconds = delaySamples.getCurrentValue() / (float) sampleRate;
            const auto feedbackRt60 = feedbackNow > 0.001f ? 3.0f * delaySeconds / -std::log10 (feedbackNow) : 0.0f;
            const auto washRt60 = juce::jlimit (roomDiffusionRt60, maxDiffusionRt60, feedbackRt60);
            const auto rt60 = minDiffusionRt60 + (washRt60 - minDiffusionRt60) * handover;

            // Recalculated when the tail length moves by more than 1%, or Shimmer's feed by more than 0.01.
            if (std::abs (rt60 - diffusionRt60) > 0.01f * diffusionRt60 || std::abs (shimmerNow - diffusionShimmer) > 0.01f)
            {
                for (auto& network : diffusers)
                    network.setDecay (rt60, diffusionHighRatio, shimmerNow);

                diffusionMakeup = diffusers[0].getMakeup();

                diffusionRt60 = rt60;
                diffusionShimmer = shimmerNow;
            }

            for (auto& network : diffusers)
                network.setShimmer (shimmerNow);

            diffusionActive = true;
        }
        else if (diffusionActive)
        {
            for (auto& network : diffusers)
                network.clear();

            diffusionActive = false;
        }

        // Modulation, in the loop and/or on the output; the position crossfades between the two.
        const auto inLoopNow = modInLoop.getNextValue();
        const auto loopMod = modulators[0].advance (inLoopNow);
        const auto outMod  = modulators[1].advance (1.0f - inLoopNow);

        const auto tapeGain = head.getGain();

        // Taps read the line before the loop moves on, like the output head.
        float taps[2] {};
        const auto tapsMix = tapsRunning ? multiTapAmount.getNextValue() : 0.0f;

        if (tapsRunning)
            renderTaps (isStereo, modulation, taps);

        if (! isStereo)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                const auto dry = data[i];
                const auto heard = head.read ([this, ch] (float x) { return readLine (ch, x); }, d);
                const auto delayed = delayLine.popSample (ch, d);

                const auto echo = filter (ch, delayed);
                auto normalWrite = tape (ch, send * dry + fb * (filtersInLoop ? echo : delayed), character);

                if (loopMod)
                    normalWrite = modulators[0].process (ch, normalWrite);

                delayLine.pushSample (ch, normalWrite + frozen * (delayed - normalWrite));

                auto heardEcho = tapeGain * outputFilter (ch, heard);

                // Multi-tap replaces the main echo with the taps. (The main
                // head's filters keep running so it comes back without a click.)
                if (tapsRunning)
                    heardEcho += tapsMix * (tapFilter (ch, taps[0]) - heardEcho);

                heardEcho = reverberate (ch, heardEcho);

                if (outMod)
                    heardEcho = modulators[1].process (ch, heardEcho);

                data[i] = dry * (1.0f - wet) + heardEcho * wet;
            }

            continue;
        }

        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const float dry[2] { left[i], right[i] };
        const float heard[2] { head.read ([this] (float x) { return readLine (0, x); }, d),
                               head.read ([this] (float x) { return readLine (1, x); }, d) };
        const float delayed[2] { delayLine.popSample (0, d), delayLine.popSample (1, d) };
        const float echo[2] { filter (0, delayed[0]), filter (1, delayed[1]) };
        const float looped[2] { filtersInLoop ? echo[0] : delayed[0], filtersInLoop ? echo[1] : delayed[1] };

        // Normal: each channel feeds back into itself.
        const float normalWrite[2] { send * dry[0] + fb * looped[0], send * dry[1] + fb * looped[1] };

        // Ping-pong: mono input -> left, left -> right at full level, right -> left via feedback.
        const float pingPongWrite[2] { 0.5f * send * (dry[0] + dry[1]) + fb * looped[1], looped[0] };

        // Frozen: loop the buffer as it is, swapping sides in ping-pong so it keeps bouncing.
        const float frozenWrite[2] { delayed[0] + pingPong * (delayed[1] - delayed[0]),
                                     delayed[1] + pingPong * (delayed[0] - delayed[1]) };

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto mixed = normalWrite[ch] + pingPong * (pingPongWrite[ch] - normalWrite[ch]);
            auto write = tape (ch, mixed, character);

            if (loopMod)
                write = modulators[0].process (ch, write);

            delayLine.pushSample (ch, write + frozen * (frozenWrite[ch] - write));
        }

        float heardEcho[2] { tapeGain * outputFilter (0, heard[0]), tapeGain * outputFilter (1, heard[1]) };

        if (tapsRunning)
            for (int ch = 0; ch < 2; ++ch)
                heardEcho[ch] += tapsMix * (tapFilter (ch, taps[ch]) - heardEcho[ch]); // taps replace the main echo

        for (int ch = 0; ch < 2; ++ch)
        {
            heardEcho[ch] = reverberate (ch, heardEcho[ch]);

            if (outMod)
                heardEcho[ch] = modulators[1].process (ch, heardEcho[ch]);
        }

        widthDelayLine.pushSample (0, heardEcho[1]);
        const auto rightEcho = widthDelayLine.popSample (0, width);

        left[i]  = dry[0] * (1.0f - wet) + heardEcho[0] * wet;
        right[i] = dry[1] * (1.0f - wet) + rightEcho * wet;
    }
}

// ---- Taps ---------------------------------------------------------------------

void DelayEngine::setTaps (const std::vector<TapSettings>& taps)
{
    const auto count = juce::jmin ((int) taps.size(), maxTaps);

    for (int i = 0; i < count; ++i)
    {
        auto& voice = voices[(size_t) i];
        const auto& tap = taps[(size_t) i];
        voice.delayMs   = tap.delayMs;
        voice.beats     = tap.beats;
        voice.gain      = tap.gain;
        voice.pan       = tap.pan;
        voice.semitones = tap.semitones;
        voice.reverse   = tap.reverse;
        voice.uid       = tap.uid; // last, so a new uid comes with its settings
    }

    numTaps = count;
}

void DelayEngine::prepareVoice (TapVoice& voice)
{
    voice.head.prepare (sampleRate);
    voice.gainNow.reset (sampleRate, tapGainSeconds);
    voice.leftGain.reset (sampleRate, tapGainSeconds);
    voice.rightGain.reset (sampleRate, tapGainSeconds);
    voice.active.reset (sampleRate, tapFadeSeconds);
    voice.active.setCurrentAndTargetValue (0.0f);
    voice.delaySmoothing = -1;
    voice.playing.uid = 0;
}

void DelayEngine::startVoice (TapVoice& voice, const TapSettings& settings)
{
    voice.playing = settings;
    voice.head.setTapestop (tapestopHeld);
    voice.head.setReverse (settings.reverse || reverseHeld);
    voice.head.setPitchSemitones (settings.semitones);
    voice.head.reset();
    voice.delaySamples.setCurrentAndTargetValue (juce::jlimit (1.0f, maxDelaySamples, (float) (settings.delayMs * 0.001 * sampleRate)));
    voice.gainNow.setCurrentAndTargetValue (settings.gain);

    const auto angle = (settings.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    voice.leftGain.setCurrentAndTargetValue (std::cos (angle));
    voice.rightGain.setCurrentAndTargetValue (std::sin (angle));

    voice.active.setCurrentAndTargetValue (0.0f);
    voice.active.setTargetValue (1.0f);
}

void DelayEngine::updateVoices (bool isStereo)
{
    const auto wanted = multiTapAmount.getTargetValue() > 0.0f || multiTapAmount.getCurrentValue() > 0.0f;

    if (! wanted)
    {
        tapsRunning = false;
        return;
    }

    if (! tapsRunning)
    {
        // Starting from silence: every tap fades in from its current settings.
        lowCutTapFilter.reset();
        highCutTapFilter.reset();
        lowCutTapFilter.setCutoffFrequency (lowCutHz.getCurrentValue());
        highCutTapFilter.setCutoffFrequency (highCutHz.getCurrentValue());

        for (auto& voice : voices)
        {
            voice.active.setCurrentAndTargetValue (0.0f);
            voice.playing.uid = 0;
        }

        tapsRunning = true;
    }

    const auto count = numTaps.load();

    for (int i = 0; i < maxTaps; ++i)
    {
        auto& voice = voices[(size_t) i];

        if (i >= count)
        {
            voice.active.setTargetValue (0.0f);
            continue;
        }

        TapSettings settings;
        settings.uid       = voice.uid.load();
        settings.beats     = voice.beats.load();
        settings.delayMs   = tapsSynced ? syncedTapMs (settings.beats) : voice.delayMs.load();
        settings.gain      = voice.gain.load();
        settings.pan       = isStereo ? juce::jlimit (-1.0f, 1.0f, voice.pan.load()) : 0.0f;
        settings.semitones = voice.semitones.load();
        settings.reverse   = voice.reverse.load();

        if (settings.uid != voice.playing.uid)
        {
            // A different tap: fade out what's playing, then start the new one from silence.
            if (voice.active.getCurrentValue() <= 0.0f)
                startVoice (voice, settings);
            else
                voice.active.setTargetValue (0.0f);

            continue;
        }

        voice.playing = settings;
        voice.active.setTargetValue (1.0f);
    }

    for (auto& voice : voices)
    {
        if (voice.active.getCurrentValue() <= 0.0f && voice.active.getTargetValue() <= 0.0f)
            continue;

        const auto& settings = voice.playing;

        if (voice.delaySmoothing != delaySmoothingSamples)
        {
            // As with the main delay time, keep any glide going from where it is.
            voice.delaySmoothing = delaySmoothingSamples;
            const auto current = voice.delaySamples.getCurrentValue();
            const auto target  = voice.delaySamples.getTargetValue();
            voice.delaySamples.reset (juce::jmax (0, delaySmoothingSamples));
            voice.delaySamples.setCurrentAndTargetValue (current);
            voice.delaySamples.setTargetValue (target);
        }

        voice.delaySamples.setTargetValue (juce::jlimit (1.0f, maxDelaySamples, (float) (settings.delayMs * 0.001 * sampleRate)));
        voice.gainNow.setTargetValue (settings.gain);

        const auto angle = (settings.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        voice.leftGain.setTargetValue (std::cos (angle));
        voice.rightGain.setTargetValue (std::sin (angle));

        voice.head.setTapestopTimeMs (tapestopMs);
        voice.head.setTapestop (tapestopHeld);
        voice.head.setReverse (settings.reverse || reverseHeld);
        voice.head.setPitchSemitones (settings.semitones);
    }
}

void DelayEngine::renderTaps (bool isStereo, float modulation, float out[2])
{
    const auto readMono = [this, isStereo] (float x)
    {
        return isStereo ? 0.5f * (readLine (0, x) + readLine (1, x)) : readLine (0, x);
    };

    for (auto& voice : voices)
    {
        if (voice.active.getCurrentValue() <= 0.0f && voice.active.getTargetValue() <= 0.0f)
            continue;

        const auto d = juce::jlimit (1.0f, maxDelaySamples, voice.delaySamples.getNextValue() + modulation);
        voice.head.advance (d);

        const auto heard = voice.head.read (readMono, d);
        const auto gain = voice.gainNow.getNextValue() * voice.active.getNextValue() * voice.head.getGain();
        const auto left = voice.leftGain.getNextValue(), right = voice.rightGain.getNextValue();

        if (isStereo)
        {
            out[0] += gain * left * heard;
            out[1] += gain * right * heard;
        }
        else
        {
            out[0] += gain * heard;
        }
    }
}

float DelayEngine::tapFilter (int channel, float sample)
{
    // The taps' own copy of the output filters, so they sound like the main echo.
    const auto low  = lowCutTapFilter.processSample (channel, sample);
    const auto afterLow = lowCutOn ? low : sample;
    const auto high = highCutTapFilter.processSample (channel, afterLow);
    return highCutOn ? high : afterLow;
}
