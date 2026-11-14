#include "Modulation.h"

void Modulator::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    msToSamples = (float) (0.001 * sampleRate);

    for (auto& line : lines)
        line.prepare ((size_t) std::ceil ((chorusCentreMs + chorusSpanMs) * msToSamples));

    for (auto& amount : amounts)
        amount.reset (sampleRate, 0.05);

    rate.reset (sampleRate, 0.1);
    depth.reset (sampleRate, 0.05);
    reset();
}

void Modulator::reset()
{
    clearState();

    for (auto& amount : amounts)
        amount.setCurrentAndTargetValue (amount.getTargetValue());

    rate.setCurrentAndTargetValue (rate.getTargetValue());
    depth.setCurrentAndTargetValue (depth.getTargetValue());
    phase = 0.0f;
}

void Modulator::clearState()
{
    for (auto& line : lines)
        line.clear();

    for (auto& channel : apX) channel.fill (0.0f);
    for (auto& channel : apY) channel.fill (0.0f);

    running = false;
}

void Modulator::setType (Type type)
{
    for (int i = 0; i < numTypes; ++i)
        amounts[(size_t) i].setTargetValue ((int) type == i + 1 ? 1.0f : 0.0f);
}

void Modulator::setRateHz (float hz)      { rate.setTargetValue (juce::jlimit (0.01f, 20.0f, hz)); }
void Modulator::setDepth (float amount01) { depth.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }

bool Modulator::advance (float newLevel)
{
    auto wanted = newLevel > 0.0f;

    if (wanted)
    {
        wanted = false;

        for (const auto& amount : amounts)
            wanted = wanted || amount.getCurrentValue() > 0.0f || amount.getTargetValue() > 0.0f;
    }

    if (! wanted)
    {
        // Next time it starts, the line and allpasses start from silence.
        running = false;
        return false;
    }

    if (! running)
    {
        clearState();
        running = true;
    }

    level = newLevel;

    for (size_t i = 0; i < (size_t) numTypes; ++i)
        now[i] = amounts[i].getNextValue();

    depthNow = depth.getNextValue();

    phase += juce::MathConstants<float>::twoPi * rate.getNextValue() / (float) sampleRate;

    if (phase >= juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;

    lfo[0] = std::sin (phase);
    lfo[1] = std::cos (phase); // a quarter cycle on, for width

    if (now[2] > 0.0f)
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            // The allpass corner sweeps exponentially, 300 Hz to four octaves up at full depth.
            const auto hz = phaserMinHz * std::exp2 (phaserOctaves * depthNow * 0.5f * (1.0f + lfo[ch]));
            const auto t = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, 0.45f * (float) sampleRate) / (float) sampleRate);
            phaserCoef[ch] = (t - 1.0f) / (t + 1.0f);
        }
    }

    return true;
}

float Modulator::process (int channel, float input)
{
    const auto ch = (size_t) juce::jlimit (0, 1, channel);
    auto out = input;

    if (now[0] > 0.0f || now[1] > 0.0f)
    {
        auto& line = lines[ch];
        line.write (input);

        if (now[0] > 0.0f)
        {
            const auto delay = (chorusCentreMs + depthNow * chorusSpanMs * lfo[ch]) * msToSamples;
            out += level * 0.5f * now[0] * (line.read (delay) - input);
        }

        if (now[1] > 0.0f)
        {
            const auto delay = (flangerCentreMs + depthNow * flangerSpanMs * lfo[ch]) * msToSamples;
            out += level * 0.5f * now[1] * (line.read (delay) - input);
        }
    }

    if (now[2] > 0.0f)
    {
        // Six first-order allpasses: y = c x + x[-1] - c y[-1], each 90 degrees at the corner.
        const auto c = phaserCoef[ch];
        auto y = input;

        for (size_t stage = 0; stage < (size_t) numStages; ++stage)
        {
            const auto x = y;
            y = c * x + apX[ch][stage] - c * apY[ch][stage];
            apX[ch][stage] = x;
            apY[ch][stage] = y;
        }

        out += level * 0.5f * now[2] * (y - input);
    }

    return out;
}
