#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DiffusionNetwork.h"
#include "Modulation.h"
#include "ReadHead.h"

/**
    Stereo feedback delay with smoothed time, feedback and mix, plus 12 dB/oct
    low cut (high-pass) and high cut (low-pass) filters on the delayed signal.

    Filters in the feedback loop shape every repeat a little more; output-only
    filters shape the echoes once without affecting what is fed back.

    Freeze crossfades the write path from (input + feedback) to the buffer's own
    unfiltered output, so the current contents loop forever without decaying.

    Ping-pong (stereo only) feeds the mono input into the left line, passes the
    left echo at full level into the right line, and feeds the right echo back
    into the left line through the feedback amount. With zero feedback that
    gives exactly two repeats: one left, then one right.

    The input send scales how much of the input is written into the delay; the
    processor uses it for Throw and the Throw Only input mode. The dry signal
    is not affected.

    Stereo width delays the right channel of the echoes by a few milliseconds,
    on the output only, so it doesn't build up in the feedback loop.

    Wear adds character to the write path, as set by the Style: a soft
    saturator with a ceiling, darkening on every pass, a low cut for the thin
    styles, sample-and-hold and fewer bits for Lo-Fi, and wow and flutter on
    the delay time. Each is crossfaded in by the Wear amount, so 0 is exactly
    the clean delay in every style. Wow and flutter fade out while frozen so
    the loop stays in time. Styles are smoothed, so switching doesn't click.

    Runaway raises feedback above 100% and pushes the saturator fully in. The
    saturator's ceiling keeps the loop bounded, and a DC blocker stops any
    offset from building up.

    Tapestop moves a separate output read head, so the feedback loop (and a
    frozen loop) is never touched: only what you hear slows to a stop, fading
    out as it goes, then spins back up in half the stop time. Once fully
    stopped and silent, the head jumps to where the spin-up will land exactly
    on the live loop; if released early, a short splice brings it back. The
    output has its own copy of the filters, fed the same signal when idle.

    Diffusion turns the delay into a reverb. The heard echoes are blended
    (equal power) into a DiffusionNetwork reverb per channel, after the loop.
    As it goes up, the delay's own feedback is handed over to the reverb's
    tail, whose length is set from what the feedback would have given; at
    full, the delay feeds the reverb once, so no separate repeats are left.
    The reverb is never inside the loop, so the loop can't run away.

    Shimmer feeds the reverb's output back into it through a pitch shifter, so
    the tail climbs by the shift on every pass. The reverb makes room for the
    feed, so Feedback still sets how long the tail lasts.

    Modulation is a chorus, flanger or phaser (Modulator), either on the write
    path, where every repeat picks up more of it (skipped while frozen, like
    Wear), or on the heard echoes after Diffusion. The position crossfades.

    Reverse also lives on the output head: two heads read backwards over
    segments one delay time long, half a segment apart, with sin² windows that
    always sum to 1. The loop is untouched, so a frozen loop plays backwards and
    comes back as it was. The heads slow with Tapestop. Both live in ReadHead.

    Multi-tap replaces the main echo with up to 16 taps: output-only read heads
    on the same line, each with its own time, level, pan, pitch and Reverse
    (which also follows the global gesture; the global Tapestop stops every
    tap). The loop still runs at the main delay time, so with feedback each tap
    repeats every delay time. The taps' sum goes through its own copy of the
    output filters, then into Diffusion and Width in place of the main echo.
    With Multi-tap off they aren't processed at all.

    While synced, tap times come from their beats at the tempo, swung by Groove
    (an off-beat eighth moves from halfway to two thirds of the beat at full)
    and moved by Feel (milliseconds ahead of or behind the beat).
*/
class DelayEngine
{
public:
    /** One tap's settings, in real units. uid identifies the tap, so a voice fades
        out and back in when it's given a different tap rather than gliding. */
    struct TapSettings
    {
        int uid = 0;
        float delayMs = 250.0f, gain = 1.0f, pan = 0.0f, semitones = 0.0f; // pan -1 (left) to 1 (right)
        float beats = 0.25f; // length in quarter notes, used instead of delayMs while synced
        bool reverse = false;
    };

    static constexpr int maxTaps = 16;

    /** What Wear does. The first is the original tape character. */
    enum class Style { studioTape = 0, tapeEcho, bucketBrigade, loFiDigital, telephone };
    static constexpr int numStyles = 5;

    void prepare (double sampleRate, int maxBlockSize, int numChannels, float maxDelayMs, float maxWidthMs);
    void reset();

    void setDelayMs (float ms);

    /** How long a delay time change takes to reach its new value. 0 jumps instantly. */
    void setDelaySmoothingMs (float ms);
    void setFeedback (float amount01);
    void setMix (float wet01);

    /** Pass 0 to bypass a filter. */
    void setLowCutHz (float hz);
    void setHighCutHz (float hz);
    void setFiltersInFeedbackLoop (bool inLoop) { filtersInLoop = inLoop; }

    /** While frozen, input is no longer written and the buffer loops at full level. */
    void setFrozen (bool shouldFreeze) { freezeAmount.setTargetValue (shouldFreeze ? 1.0f : 0.0f); }
    void setFreezeFadeMs (float ms);

    /** Linear gain of the input into the delay line. */
    void setInputSend (float gain) { inputSend.setTargetValue (juce::jmax (0.0f, gain)); }

    void setPingPong (bool shouldPingPong) { pingPongAmount.setTargetValue (shouldPingPong ? 1.0f : 0.0f); }
    void setStereoWidthMs (float ms);

    void setWear (float amount01)          { wear.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }
    void setStyle (int styleIndex);
    void setRunaway (bool shouldRunAway)   { runawayAmount.setTargetValue (shouldRunAway ? 1.0f : 0.0f); }
    void setRunawayDrive (float amount01)  { runawayDrive.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }

    void setTapestop (bool shouldStop)     { tapestopHeld = shouldStop; head.setTapestop (shouldStop); }
    void setReverse (bool shouldReverse)   { reverseHeld = shouldReverse; head.setReverse (shouldReverse); }
    void setDiffusion (float amount01)     { diffusion.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }
    void setTapestopTimeMs (float ms);

    void setShimmer (float amount01)       { shimmer.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }
    void setShimmerSemitones (float semitones);

    void setModulationType (int type);
    void setModulationRateHz (float hz);
    void setModulationDepth (float amount01);
    void setModulationInFeedbackLoop (bool inLoop) { modInLoop.setTargetValue (inLoop ? 1.0f : 0.0f); }

    void setMultiTap (bool shouldUseTaps)  { multiTapAmount.setTargetValue (shouldUseTaps ? 1.0f : 0.0f); }

    /** While synced, tap times come from their beats at this tempo. */
    void setTapTempo (bool synced, double bpm) { tapsSynced = synced; tapsBpm = juce::jmax (1.0, bpm); }

    /** Swing on synced taps, 0 to 1 (full moves an off-beat eighth to the triplet). */
    void setGroove (float swing01)         { groove = juce::jlimit (0.0f, 1.0f, swing01); }

    /** Milliseconds added to every synced tap time: negative is ahead of the beat. */
    void setFeelMs (float ms)              { feelMs = ms; }

    /** Any thread; taps past maxTaps are ignored. */
    void setTaps (const std::vector<TapSettings>& taps);

    void process (juce::AudioBuffer<float>& buffer);

private:
    /** The character stage's settings for one sample, shared by both channels. */
    struct CharacterSettings
    {
        float amount, ceiling, darkCoef;  // saturation amount and ceiling; darkening one-pole coefficient (1 is off)
        float thin, thinCoef;             // low cut amount and coefficient
        float lofi, lofiHoldStep, lofiSteps; // sample-and-hold amount, its phase step, and quantisation steps
    };

    float filter (int channel, float sample);
    float outputFilter (int channel, float sample);

    /** Blends the heard echo into the reverb; untouched while Diffusion is 0. */
    float reverberate (int channel, float echo);

    /** The line read at a delay, clamped to what it holds. Call before the loop's popSample for this sample. */
    float readLine (int channel, float delay);

    /** DC blocking, low cut, saturation, lo-fi and darkening, each scaled so 0 leaves the sample untouched. */
    float tape (int channel, float sample, const CharacterSettings&);

    /** A synced tap's time, with Groove and Feel. */
    float syncedTapMs (float beats) const;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> widthDelayLine;
    juce::dsp::StateVariableTPTFilter<float> lowCutFilter, highCutFilter;
    juce::dsp::StateVariableTPTFilter<float> lowCutOutFilter, highCutOutFilter;
    juce::SmoothedValue<float> delaySamples, feedback, mix, freezeAmount, pingPongAmount, widthSamples, inputSend;
    juce::SmoothedValue<float> wear, runawayAmount, runawayDrive, diffusion;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCutHz { 20.0f }, highCutHz { 20000.0f };
    bool lowCutOn = false, highCutOn = false, filtersInLoop = true;
    int delaySmoothingSamples = -1, freezeFadeSamples = -1;
    double sampleRate = 44100.0;
    float maxDelaySamples = 1.0f, maxWidthSamples = 0.0f;

    // Character state. The style's settings are smoothed so switching styles doesn't click.
    int style = 0;
    juce::SmoothedValue<float> styleCeiling, styleDarkCoef, styleThinOn, styleThinCoef, styleWowMs, styleFlutterMs, styleDrift, styleLofi;
    float dcState[2] {}, thinState[2] {}, darkState[2] {}, held[2] {}, holdPhase[2] {};
    float dcCoef = 0.0f, lofiMaxHold = 1.0f;
    float wowPhase = 0.0f, flutterPhase = 0.0f, wowIncrement = 0.0f, flutterIncrement = 0.0f;
    float drift = 0.0f, driftTarget = 0.0f, driftCoef = 0.0f;
    int driftCountdown = 0;
    juce::Random random;

    // The output head: Tapestop and Reverse act on what you hear, never the loop.
    ReadHead head;
    bool tapestopHeld = false, reverseHeld = false;
    float maxReadSamples = 1.0f, tapestopMs = 500.0f;

    // Modulation: one on the write path, one on the output; the position crossfades between them.
    std::array<Modulator, 2> modulators;
    juce::SmoothedValue<float> modInLoop { 1.0f };

    // ---- Taps -----------------------------------------------------------------
    struct TapVoice
    {
        // Written by the message thread.
        std::atomic<int> uid { 0 };
        std::atomic<float> delayMs { 250.0f }, beats { 0.25f }, gain { 1.0f }, pan { 0.0f }, semitones { 0.0f };
        std::atomic<bool> reverse { false };

        // Audio thread only.
        TapSettings playing;   // what this voice is playing; follows the settings while uid matches
        int delaySmoothing = -1;
        ReadHead head;
        juce::SmoothedValue<float> delaySamples, gainNow, leftGain, rightGain, active;
    };

    void prepareVoice (TapVoice&);
    void startVoice (TapVoice&, const TapSettings&);
    void updateVoices (bool isStereo);
    void renderTaps (bool isStereo, float modulation, float out[2]);
    float tapFilter (int channel, float sample);

    std::array<TapVoice, maxTaps> voices;
    std::atomic<int> numTaps { 0 };
    juce::SmoothedValue<float> multiTapAmount;
    bool tapsRunning = false, tapsSynced = false;
    double tapsBpm = 120.0;
    float groove = 0.0f, feelMs = 0.0f;
    juce::dsp::StateVariableTPTFilter<float> lowCutTapFilter, highCutTapFilter;

    std::array<DiffusionNetwork, 2> diffusers;
    juce::SmoothedValue<float> shimmer;
    float shimmerSemitones = 12.0f;
    float diffusionCos = 1.0f, diffusionSin = 0.0f, diffusionSetting = 0.0f, diffusionRt60 = 0.0f, diffusionShimmer = -1.0f, diffusionMakeup = 1.0f;
    bool diffusionActive = false;
};
