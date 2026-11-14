#pragma once

#include <juce_dsp/juce_dsp.h>
#include "PitchShifter.h"

/**
    The reverb behind Diffusion: a feedback delay network (Jot's design).

    The input is first smeared by four allpasses, then spread across sixteen
    delay lines. The lines are mixed by a 16×16 Hadamard matrix. Each line has
    a one-pole absorption filter that sets the decay: 60 dB in rt60 seconds at
    low frequencies, faster at the top, as in a room. The output taps the lines
    with a different sign pattern from the input, and is scaled so a burst of
    noise comes out at about the same level it went in (getMakeup).

    Each line's length drifts slowly by up to ±0.5 ms, at its own rate. That
    smears the network's resonances, so sustained tones aren't picked out and
    boosted and the tail doesn't ring metallically.

    Shimmer feeds the previous sample's output back into the input through a
    pitch shifter, so every pass through the network climbs (or falls) by the
    shift. The feed is trimmed below 150 Hz and above 8 kHz, so repeated
    shifting can't pile up at either end, and soft-limited as a safety net.
    setDecay takes the send too: the lines' own decay is shortened to make room
    for the energy the feed puts back, so the tail is still rt60 long.

    It sits after the delay's feedback loop, never inside it, so it can have
    make-up gain without any risk of the loop running away.
*/
class DiffusionNetwork
{
public:
    static constexpr int numLines = 16;
    static constexpr int numPreStages = 4;

    void prepare (const float (&lineMs)[numLines], const float (&preMs)[numPreStages], double newSampleRate)
    {
        sampleRate = newSampleRate;

        for (size_t i = 0; i < (size_t) numLines; ++i)
            lines[i].assign ((size_t) juce::jmax (1, juce::roundToInt (lineMs[i] * 0.001 * sampleRate)), 0.0f);

        for (size_t i = 0; i < (size_t) numPreStages; ++i)
            pre[i].assign ((size_t) juce::jmax (1, juce::roundToInt (preMs[i] * 0.001 * sampleRate)), 0.0f);

        // Modulation: one slow rate per line, spread from 0.15 to 0.9 Hz.
        modDepth = modDepthMs * 0.001f * (float) sampleRate;

        for (size_t i = 0; i < (size_t) numLines; ++i)
        {
            const auto hz = 0.15f + 0.75f * (float) i / (float) (numLines - 1);
            const auto step = juce::MathConstants<float>::twoPi * hz / (float) sampleRate;
            rotCos[i] = std::cos (step);
            rotSin[i] = std::sin (step);
            const auto start = juce::MathConstants<float>::twoPi * (float) i / (float) numLines;
            oscCos[i] = std::cos (start);
            oscSin[i] = std::sin (start);
        }

        shifter.prepare (sampleRate, shimmerWindowMs);
        shimmerLowCoef  = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * shimmerLowHz  / (float) sampleRate);
        shimmerHighCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * shimmerHighHz / (float) sampleRate);

        setDecay (2.0f, 0.5f, 0.0f);
        clear();
    }

    /** Time to fall 60 dB at low frequencies; the top end takes highRatio times as long.
        `send` is Shimmer's feed (setShimmer), which the lines make room for. */
    void setDecay (float rt60Seconds, float highRatio, float send)
    {
        auto meanPass = 0.0f;

        // Each pass, the feed puts back about send² / N of the energy the lines
        // hold (the shifted signal is uncorrelated with what's in them), so the
        // lines keep that much less.
        const auto feedShare = send * send / (float) numLines;

        for (size_t i = 0; i < (size_t) numLines; ++i)
        {
            const auto length = (float) lines[i].size();
            const auto lowTarget  = std::pow (10.0f, -3.0f * length / (rt60Seconds * (float) sampleRate));
            const auto highTarget = std::pow (10.0f, -3.0f * length / (rt60Seconds * highRatio * (float) sampleRate));
            const auto lowGain  = feedShare > 0.0f ? std::sqrt (juce::jmax (0.0001f, lowTarget * lowTarget - feedShare)) : lowTarget;
            const auto highGain = feedShare > 0.0f ? std::sqrt (juce::jmax (0.0001f, highTarget * highTarget - feedShare)) : highTarget;

            // H(z) = lowGain (1 - pole) / (1 - pole z^-1): lowGain at DC, highGain at Nyquist.
            poles[i] = (lowGain - highGain) / (lowGain + highGain);
            inputGains[i] = lowGain * (1.0f - poles[i]);
            meanPass += length;
        }

        // Each pass through the lines hands 1/N of the stored energy to the
        // output and keeps g² of it, so a unit of input comes out as about
        // 1 / (N (1 - g²)). Make-up gain undoes that. (With Shimmer the lines
        // keep less but the feed puts it back, so the total is unchanged.)
        meanPass /= (float) numLines;
        const auto passGainSquared = std::pow (10.0f, -6.0f * meanPass / (rt60Seconds * (float) sampleRate));
        makeup = makeupTrim * std::sqrt ((float) numLines * (1.0f - passGainSquared));
    }

    float getMakeup() const { return makeup; }

    /** Shimmer's feed, 0 to about 0.6; 0 leaves the shifter out of the path. */
    void setShimmer (float send)        { shimmerSend = juce::jmax (0.0f, send); }
    void setShimmerRatio (float ratio)  { shifter.setRatio (ratio); }

    void clear()
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);

        for (auto& stage : pre)
            std::fill (stage.begin(), stage.end(), 0.0f);

        index.fill (0);
        preIndex.fill (0);
        absorption.fill (0.0f);

        shifter.clear();
        shimmerLow = shimmerHigh = lastOutput = 0.0f;
        shimmerOn = false;
    }

    /** Returns the reverb only (no dry signal), before make-up gain. */
    float process (float input)
    {
        if (shimmerSend > 0.0f)
        {
            if (! shimmerOn)
            {
                // Coming in from off: nothing stale in the shifter.
                shifter.clear();
                shimmerLow = shimmerHigh = 0.0f;
                shimmerOn = true;
            }

            auto shifted = shifter.process (lastOutput);
            shimmerLow += shimmerLowCoef * (shifted - shimmerLow);
            shifted -= shimmerLow;
            shimmerHigh += shimmerHighCoef * (shifted - shimmerHigh);
            const auto fed = shimmerSend * shimmerHigh;
            input += shimmerCeiling * std::tanh (fed / shimmerCeiling);
        }
        else
        {
            shimmerOn = false;
        }

        // Pre-diffusion: Schroeder allpasses thicken the attack.
        for (size_t i = 0; i < (size_t) numPreStages; ++i)
        {
            auto& slot = pre[i][preIndex[i]];
            const auto delayed = slot;
            const auto w = input + preGain * delayed;
            slot = w;
            if (++preIndex[i] == pre[i].size()) preIndex[i] = 0;
            input = delayed - preGain * w;
        }

        std::array<float, numLines> z;
        auto output = 0.0f;

        for (size_t i = 0; i < (size_t) numLines; ++i)
        {
            // Advance this line's oscillator (a rotating phasor; renormalised so it never drifts).
            const auto c = oscCos[i] * rotCos[i] - oscSin[i] * rotSin[i];
            const auto s = oscSin[i] * rotCos[i] + oscCos[i] * rotSin[i];
            const auto norm = 1.5f - 0.5f * (c * c + s * s);
            oscCos[i] = c * norm;
            oscSin[i] = s * norm;

            // The slot at index holds the sample from a full line length ago;
            // reading ahead of it by `ahead` samples shortens the delay by that much.
            const auto ahead = modDepth * (1.0f + oscSin[i]);
            const auto whole = (size_t) ahead;
            const auto frac = ahead - (float) whole;
            const auto size = lines[i].size();
            const auto a = lines[i][(index[i] + whole) % size];
            const auto b = lines[i][(index[i] + whole + 1) % size];
            const auto read = a + frac * (b - a);

            absorption[i] = inputGains[i] * read + poles[i] * absorption[i];
            z[i] = absorption[i];
            output += outputSign (i) * z[i];
        }

        hadamard (z);

        for (size_t i = 0; i < (size_t) numLines; ++i)
        {
            lines[i][index[i]] = z[i] * invSqrtN + inputSign (i) * invSqrtN * input;
            if (++index[i] == lines[i].size()) index[i] = 0;
        }

        lastOutput = output * invSqrtN;
        return lastOutput;
    }

private:
    static constexpr float invSqrtN = 0.25f; // 1/√16
    static constexpr float preGain  = 0.6f;
    static constexpr float modDepthMs = 0.5f;
    // Measured (EngineTests, "comes out at about input level"): corrects the
    // estimate above for what the modulation's interpolation takes out.
    static constexpr float makeupTrim = 1.84f;

    // Shimmer: the shifter's window, the feed's band, and the soft limit on it.
    static constexpr float shimmerWindowMs = 80.0f;
    static constexpr float shimmerLowHz = 150.0f, shimmerHighHz = 8000.0f;
    static constexpr float shimmerCeiling = 1.0f;

    // Different sign patterns in and out, so no line is favoured.
    static float inputSign (size_t i)  { return (0x5A3Cu >> i) & 1u ? 1.0f : -1.0f; }
    static float outputSign (size_t i) { return (0x36C9u >> i) & 1u ? 1.0f : -1.0f; }

    /** Unnormalised fast Walsh-Hadamard transform, in place. */
    static void hadamard (std::array<float, numLines>& v)
    {
        for (size_t half = 1; half < (size_t) numLines; half *= 2)
            for (size_t i = 0; i < (size_t) numLines; i += half * 2)
                for (size_t j = i; j < i + half; ++j)
                {
                    const auto a = v[j], b = v[j + half];
                    v[j] = a + b;
                    v[j + half] = a - b;
                }
    }

    std::array<std::vector<float>, numLines> lines;
    std::array<size_t, numLines> index {};
    std::array<float, numLines> absorption {}, poles {}, inputGains {};
    std::array<std::vector<float>, numPreStages> pre;
    std::array<size_t, numPreStages> preIndex {};
    std::array<float, numLines> oscCos {}, oscSin {}, rotCos {}, rotSin {};
    double sampleRate = 44100.0;
    float makeup = 1.0f, modDepth = 0.0f;

    PitchShifter shifter;
    float shimmerSend = 0.0f, shimmerLowCoef = 0.0f, shimmerHighCoef = 1.0f;
    float shimmerLow = 0.0f, shimmerHigh = 0.0f, lastOutput = 0.0f;
    bool shimmerOn = false;
};
