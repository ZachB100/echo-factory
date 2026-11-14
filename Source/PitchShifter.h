#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

/**
    A pitch shifter on its own short line, for signals that aren't on the delay
    line (Shimmer's feed inside the reverb). Two heads sweep a window at
    (1 - ratio) samples per sample, half a window apart, with sin² windows that
    sum to 1: the same scheme as ReadHead's pitch. Unity gain.
*/
class PitchShifter
{
public:
    void prepare (double sampleRate, float windowMs)
    {
        window = juce::jmax (2.0f, windowMs * 0.001f * (float) sampleRate);
        buffer.assign ((size_t) std::ceil (window) + 3, 0.0f);
        clear();
    }

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
        phase = 0.0f;
    }

    /** 2 is an octave up, 0.5 an octave down. */
    void setRatio (float newRatio) { ratio = newRatio; }

    float process (float input)
    {
        buffer[writeIndex] = input;

        const auto phaseB = std::fmod (phase + 0.5f * window, window);
        const auto windowA = juce::square (std::sin (juce::MathConstants<float>::pi * phase / window));
        const auto out = windowA * read (phase) + (1.0f - windowA) * read (phaseB);

        if (++writeIndex == buffer.size())
            writeIndex = 0;

        // Reading `ratio` samples per sample moves the heads by (1 - ratio).
        phase += 1.0f - ratio;
        while (phase >= window) phase -= window;
        while (phase < 0.0f)    phase += window;

        return out;
    }

private:
    /** The sample written `delay` samples ago (0 is this one), linearly interpolated. */
    float read (float delay) const
    {
        auto position = (float) writeIndex - delay;

        if (position < 0.0f)
            position += (float) buffer.size();

        const auto whole = (size_t) position;
        const auto frac = position - (float) whole;
        const auto a = buffer[whole];
        const auto b = buffer[(whole + 1) % buffer.size()];
        return a + frac * (b - a);
    }

    std::vector<float> buffer;
    size_t writeIndex = 0;
    float window = 2.0f, phase = 0.0f, ratio = 2.0f;
};
