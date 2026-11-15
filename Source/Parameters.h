#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    namespace ID
    {
        inline const juce::ParameterID delayTimeMs  { "delayTimeMs",  1 };
        inline const juce::ParameterID timeSmoothingMs { "timeSmoothingMs", 1 };
        inline const juce::ParameterID sync         { "sync",         1 };
        inline const juce::ParameterID syncDivision { "syncDivision", 1 };
        inline const juce::ParameterID feedback     { "feedback",     1 };
        inline const juce::ParameterID mix          { "mix",          1 };
        inline const juce::ParameterID lowCut       { "lowCut",       1 };
        inline const juce::ParameterID highCut      { "highCut",      1 };
        inline const juce::ParameterID filterPos    { "filterPos",    1 };
        inline const juce::ParameterID freeze       { "freeze",       1 };
        inline const juce::ParameterID pingPong     { "pingPong",     1 };
        inline const juce::ParameterID stereoWidthMs { "stereoWidthMs", 1 };

        // Performance gestures (see docs/perform-map.md).
        inline const juce::ParameterID throwGesture { "throw",        1 };
        inline const juce::ParameterID tapestop     { "tapestop",     1 };
        inline const juce::ParameterID runaway      { "runaway",      1 };
        inline const juce::ParameterID reverse      { "reverse",      1 };
        inline const juce::ParameterID reset        { "reset",        1 };
        inline const juce::ParameterID inputMode    { "inputMode",    1 };
        inline const juce::ParameterID throwLevelDb { "throwLevelDb", 1 };
        inline const juce::ParameterID freezeFadeMs { "freezeFadeMs", 1 };
        inline const juce::ParameterID runawayDrive { "runawayDrive", 1 };
        inline const juce::ParameterID tapestopTimeMs { "tapestopTimeMs", 1 };

        // Character
        inline const juce::ParameterID wear         { "wear",         1 };
        inline const juce::ParameterID diffusion    { "diffusion",    1 };

        // Added after release: version hint 2.
        inline const juce::ParameterID multiTap     { "multiTap",     2 };

        // Added after release: version hint 3.
        inline const juce::ParameterID style        { "style",        3 };
        inline const juce::ParameterID shimmer      { "shimmer",      3 };
        inline const juce::ParameterID shimmerPitch { "shimmerPitch", 3 };
        inline const juce::ParameterID modType      { "modType",      3 };
        inline const juce::ParameterID modRate      { "modRate",      3 };
        inline const juce::ParameterID modDepth     { "modDepth",     3 };
        inline const juce::ParameterID modPos       { "modPos",       3 };
        inline const juce::ParameterID feelMs       { "feelMs",       3 };
        inline const juce::ParameterID groove       { "groove",       3 };

        // Added after release: version hint 4. Brandon, the chaos mode, and its controls.
        inline const juce::ParameterID brandon         { "brandon",         4 };
        inline const juce::ParameterID brandonFeedback { "brandonFeedback", 4 };
        inline const juce::ParameterID brandonOrbit    { "brandonOrbit",    4 };
        inline const juce::ParameterID brandonCrush    { "brandonCrush",    4 };
        inline const juce::ParameterID brandonPitch    { "brandonPitch",    4 };
        inline const juce::ParameterID brandonChaos    { "brandonChaos",    4 };
    }

    /** The performance gestures, in MIDI note order (Throw, Freeze, Tapestop, Runaway, Reverse). */
    const std::array<const juce::ParameterID*, 5>& getGestureIDs();

    constexpr float minDelayMs = 1.0f;
    constexpr float maxDelayMs = 5000.0f;

    // How long the delay time takes to glide to a new value.
    constexpr float maxTimeSmoothingMs     = 2000.0f;
    constexpr float defaultTimeSmoothingMs = 50.0f;

    // Extra delay on the right channel of the echoes.
    constexpr float maxStereoWidthMs = 100.0f;

    // Send level into the delay while Throw is on.
    constexpr float minThrowLevelDb     = -12.0f;
    constexpr float maxThrowLevelDb     = 12.0f;
    constexpr float defaultThrowLevelDb = 6.0f;

    // How long Freeze takes to fade in and out.
    constexpr float minFreezeFadeMs     = 10.0f;
    constexpr float maxFreezeFadeMs     = 2000.0f;
    constexpr float defaultFreezeFadeMs = 20.0f;

    // How long Tapestop takes to stop; spin-up takes half as long.
    constexpr float minTapestopMs     = 100.0f;
    constexpr float maxTapestopMs     = 2000.0f;
    constexpr float defaultTapestopMs = 500.0f;

    // At these extremes the filter is bypassed and reads as "Off".
    constexpr float lowCutMinHz  = 20.0f;
    constexpr float lowCutMaxHz  = 2000.0f;
    constexpr float highCutMinHz = 500.0f;
    constexpr float highCutMaxHz = 20000.0f;

    // Modulation LFO rate.
    constexpr float minModRateHz     = 0.05f;
    constexpr float maxModRateHz     = 8.0f;
    constexpr float defaultModRateHz = 0.6f;

    // Feel: how far synced echoes sit ahead of (negative) or behind the beat.
    constexpr float maxFeelMs = 50.0f;

    // Every Brandon control starts here.
    constexpr float brandonDefaultPercent = 67.0f;

    enum class FilterPosition { inFeedbackLoop = 0, outputOnly = 1 };
    const juce::StringArray& getFilterPositionNames();

    enum class InputMode { always = 0, throwOnly = 1 };
    const juce::StringArray& getInputModeNames();

    /** Echo styles, in DelayEngine::Style order. */
    const juce::StringArray& getStyleNames();

    /** Modulation types, in Modulator::Type order (Off first). */
    const juce::StringArray& getModulationTypeNames();

    struct ShimmerPitch
    {
        const char* name;  // e.g. "Octave up"
        int semitones;
    };

    const std::vector<ShimmerPitch>& getShimmerPitches();
    int getDefaultShimmerPitchIndex();

    struct SyncDivision
    {
        const char* shortName;   // shown in the UI, e.g. "1/8D"
        const char* spokenName;  // read by screen readers, e.g. "1/8 dotted"
        double beats;            // length in quarter notes
    };

    const std::vector<SyncDivision>& getSyncDivisions();
    int getDefaultSyncDivisionIndex();

    /** Delay Time's range, shared by the main knob and the tap Time knobs. */
    juce::NormalisableRange<float> getDelayTimeRange();

    /** A sync division's length at a tempo, within the delay range. */
    float divisionToMs (int divisionIndex, double bpm);

    juce::String formatMilliseconds (float ms, bool spoken);

    /** "250", "250 ms", "1.5 s", "2 seconds" -> milliseconds. */
    float parseMilliseconds (const juce::String& text);

    /** e.g. "+10 ms" / "plus 10 milliseconds", "0 ms" / "0 milliseconds". */
    juce::String formatSignedMilliseconds (float ms, bool spoken);

    /** "Off" when offHz is reached, otherwise e.g. "250 Hz" / "2.5 kilohertz". */
    juce::String formatFrequency (float hz, float offHz, bool spoken);

    /** e.g. "0.60 Hz" / "0.6 hertz", "2.5 Hz" / "2.5 hertz". */
    juce::String formatHertz (float hz, bool spoken);

    /** e.g. "+6.0 dB" / "plus 6 decibels". */
    juce::String formatDecibels (float db, bool spoken);

    /** e.g. "+7 st" / "plus 7 semitones". */
    juce::String formatSemitones (int semitones, bool spoken);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
