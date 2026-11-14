#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

/**
    Chorus, flanger or phaser on one or two channels, mixed half and half with
    the dry signal so the gain never passes 1: safe inside the feedback loop.

    One sine LFO drives both channels, the right a quarter cycle on, so the
    effect is wide in stereo. Chorus and flanger read a short line (chorus 3 to
    13 ms, flanger 0.5 to 4.5 ms at full depth); the phaser sweeps six
    first-order allpasses from 300 Hz over up to four octaves.

    Each type has its own crossfade, so changing type or turning it off never
    clicks. With every crossfade at 0 the output is the input, bit-exact, and
    advance() returns false so the caller can skip process().
*/
class Modulator
{
public:
    enum class Type { off = 0, chorus, flanger, phaser };
    static constexpr int numTypes = 3; // the effects, not Off

    void prepare (double sampleRate);
    void reset();

    void setType (Type);
    void setRateHz (float hz);
    void setDepth (float amount01);

    /** Once per sample, before process(): moves the LFO and the crossfades.
        `level` scales the whole effect. Returns false when there's nothing to do. */
    bool advance (float level);

    float process (int channel, float input);

private:
    static constexpr int numStages = 6;
    static constexpr float chorusCentreMs = 8.0f,  chorusSpanMs = 5.0f;
    static constexpr float flangerCentreMs = 2.5f, flangerSpanMs = 2.0f;
    static constexpr float phaserMinHz = 300.0f,   phaserOctaves = 4.0f;

    /** A short line read at a fractional delay. */
    struct Line
    {
        void prepare (size_t maxDelaySamples)   { buffer.assign (maxDelaySamples + 3, 0.0f); clear(); }
        void clear()                            { std::fill (buffer.begin(), buffer.end(), 0.0f); writeIndex = 0; }
        void write (float x)                    { buffer[writeIndex] = x; if (++writeIndex == buffer.size()) writeIndex = 0; }

        /** `delay` samples before the last write (1 is the last sample written). */
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
    };

    void clearState();

    std::array<Line, 2> lines;
    std::array<juce::SmoothedValue<float>, numTypes> amounts;
    juce::SmoothedValue<float> rate { 0.6f }, depth { 0.5f };
    float phase = 0.0f, level = 0.0f, depthNow = 0.0f;
    std::array<float, 2> lfo {}, phaserCoef {};
    std::array<float, numTypes> now {};
    std::array<std::array<float, numStages>, 2> apX {}, apY {};
    double sampleRate = 44100.0;
    float msToSamples = 44.1f;
    bool running = false;
};
