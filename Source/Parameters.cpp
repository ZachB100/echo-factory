#include "Parameters.h"

namespace Params
{
const std::vector<SyncDivision>& getSyncDivisions()
{
    static const std::vector<SyncDivision> divisions {
        { "1/64",   "1/64 note",           0.0625 },
        { "1/32T",  "1/32 triplet",        0.125 * 2.0 / 3.0 },
        { "1/32",   "1/32 note",           0.125 },
        { "1/16T",  "1/16 triplet",        0.25 * 2.0 / 3.0 },
        { "1/16",   "1/16 note",           0.25 },
        { "1/16D",  "1/16 dotted",         0.375 },
        { "1/8T",   "1/8 triplet",         0.5 * 2.0 / 3.0 },
        { "1/8",    "1/8 note",            0.5 },
        { "1/8D",   "1/8 dotted",          0.75 },
        { "1/4T",   "1/4 triplet",         2.0 / 3.0 },
        { "1/4",    "1/4 note",            1.0 },
        { "1/4D",   "1/4 dotted",          1.5 },
        { "1/2T",   "1/2 triplet",         4.0 / 3.0 },
        { "1/2",    "1/2 note",            2.0 },
        { "1/2D",   "1/2 dotted",          3.0 },
        { "1 bar",  "1 bar",               4.0 },
        { "2 bars", "2 bars",              8.0 },
    };
    return divisions;
}

const std::array<const juce::ParameterID*, 5>& getGestureIDs()
{
    static const std::array<const juce::ParameterID*, 5> ids {
        &ID::throwGesture, &ID::freeze, &ID::tapestop, &ID::runaway, &ID::reverse
    };
    return ids;
}

int getDefaultSyncDivisionIndex() { return 7; } // 1/8 note

juce::NormalisableRange<float> getDelayTimeRange()
{
    juce::NormalisableRange<float> range { minDelayMs, maxDelayMs, 1.0f };
    range.setSkewForCentre (500.0f);
    return range;
}

float divisionToMs (int divisionIndex, double bpm)
{
    const auto& divisions = getSyncDivisions();
    const auto index = juce::jlimit (0, (int) divisions.size() - 1, divisionIndex);
    const auto ms = divisions[(size_t) index].beats * 60000.0 / juce::jmax (1.0, bpm);
    return juce::jlimit (minDelayMs, maxDelayMs, (float) ms);
}

juce::String formatMilliseconds (float ms, bool spoken)
{
    if (ms >= 1000.0f)
    {
        const auto seconds = juce::String (ms / 1000.0f, 2);
        return spoken ? seconds + " seconds" : seconds + " s";
    }

    const auto value = juce::String (juce::roundToInt (ms));
    return spoken ? value + (value == "1" ? " millisecond" : " milliseconds") : value + " ms";
}

float parseMilliseconds (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const auto number = t.getFloatValue();
    const bool isSeconds = (t.endsWith ("s") && ! t.endsWith ("ms")) || t.contains ("sec");
    return isSeconds ? number * 1000.0f : number;
}

juce::String formatSignedMilliseconds (float ms, bool spoken)
{
    const auto whole = juce::roundToInt (ms);

    if (whole == 0)
        return spoken ? "0 milliseconds" : "0 ms";

    const auto magnitude = juce::String (std::abs (whole));

    if (! spoken)
        return (whole > 0 ? "+" : "-") + magnitude + " ms";

    return (whole > 0 ? "plus " : "minus ") + magnitude + (std::abs (whole) == 1 ? " millisecond" : " milliseconds");
}

const juce::StringArray& getFilterPositionNames()
{
    static const juce::StringArray names { "In Feedback Loop", "Output Only" };
    return names;
}

const juce::StringArray& getInputModeNames()
{
    static const juce::StringArray names { "Always", "Throw Only" };
    return names;
}

const juce::StringArray& getStyleNames()
{
    static const juce::StringArray names { "Studio Tape", "Tape Echo", "Bucket Brigade", "Lo-Fi Digital", "Telephone" };
    return names;
}

const juce::StringArray& getModulationTypeNames()
{
    static const juce::StringArray names { "Off", "Chorus", "Flanger", "Phaser" };
    return names;
}

const std::vector<ShimmerPitch>& getShimmerPitches()
{
    static const std::vector<ShimmerPitch> pitches {
        { "Octave down",          -12 },
        { "Fifth down",            -7 },
        { "Fourth down",           -5 },
        { "Major third up",         4 },
        { "Fourth up",              5 },
        { "Fifth up",               7 },
        { "Octave up",             12 },
        { "Octave and a fifth up", 19 },
        { "Two octaves up",        24 },
    };
    return pitches;
}

int getDefaultShimmerPitchIndex() { return 6; } // octave up

juce::String formatDecibels (float db, bool spoken)
{
    if (! spoken)
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";

    const auto rounded = std::round (db * 10.0f) / 10.0f;
    auto magnitude = juce::String (std::abs (rounded), 1);

    if (magnitude.endsWith (".0"))
        magnitude = magnitude.dropLastCharacters (2);

    if (juce::exactlyEqual (rounded, 0.0f))
        return "0 decibels";

    return (rounded > 0.0f ? "plus " : "minus ") + magnitude + (magnitude == "1" ? " decibel" : " decibels");
}

juce::String formatSemitones (int semitones, bool spoken)
{
    if (! spoken)
        return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";

    if (semitones == 0)
        return "0 semitones";

    const auto magnitude = std::abs (semitones);
    return (semitones > 0 ? "plus " : "minus ") + juce::String (magnitude) + (magnitude == 1 ? " semitone" : " semitones");
}

juce::String formatFrequency (float hz, float offHz, bool spoken)
{
    if (juce::approximatelyEqual (hz, offHz))
        return "Off";

    if (hz >= 1000.0f)
    {
        const auto khz = juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2);
        return khz + (spoken ? " kilohertz" : " kHz");
    }

    return juce::String (juce::roundToInt (hz)) + (spoken ? " hertz" : " Hz");
}

juce::String formatHertz (float hz, bool spoken)
{
    auto text = juce::String (hz, hz < 1.0f ? 2 : 1);

    // Spoken: "0.60" -> "0.6", "2.0" -> "2".
    if (spoken)
    {
        while (text.contains (".") && text.endsWith ("0"))
            text = text.dropLastCharacters (1);

        if (text.endsWith ("."))
            text = text.dropLastCharacters (1);
    }

    return text + (spoken ? " hertz" : " Hz");
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto millisecondAttributes = []
    {
        return AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return formatMilliseconds (v, false); })
            .withValueFromStringFunction ([] (const String& text) { return parseMilliseconds (text); });
    };

    auto onOffAttributes = []
    {
        return AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool v, int) { return v ? String ("On") : String ("Off"); });
    };

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::delayTimeMs, "Delay Time", getDelayTimeRange(), 400.0f, millisecondAttributes()));

    NormalisableRange<float> smoothingRange { 0.0f, maxTimeSmoothingMs, 1.0f };
    smoothingRange.setSkewForCentre (250.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::timeSmoothingMs, "Time Smoothing", smoothingRange, defaultTimeSmoothingMs, millisecondAttributes()));

    layout.add (std::make_unique<AudioParameterBool> (ID::sync, "Tempo Sync", false, onOffAttributes()));

    StringArray divisionNames;
    for (const auto& d : getSyncDivisions())
        divisionNames.add (d.spokenName);

    layout.add (std::make_unique<AudioParameterChoice> (
        ID::syncDivision, "Sync Division", divisionNames, getDefaultSyncDivisionIndex()));

    auto percentAttributes = []
    {
        return AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })
            .withValueFromStringFunction ([] (const String& text) { return text.getFloatValue(); });
    };

    const NormalisableRange<float> percentRange { 0.0f, 100.0f, 0.1f };

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::feedback, "Feedback", NormalisableRange<float> { 0.0f, 95.0f, 0.1f }, 35.0f, percentAttributes()));

    layout.add (std::make_unique<AudioParameterFloat> (ID::mix, "Mix", percentRange, 30.0f, percentAttributes()));

    auto frequencyAttributes = [] (float offHz)
    {
        return AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([offHz] (float v, int) { return formatFrequency (v, offHz, false); })
            .withValueFromStringFunction ([offHz] (const String& text)
            {
                const auto t = text.trim().toLowerCase();

                if (t == "off")
                    return offHz;

                const auto number = t.getFloatValue();
                return t.contains ("k") ? number * 1000.0f : number;
            });
    };

    NormalisableRange<float> lowCutRange { lowCutMinHz, lowCutMaxHz, 1.0f };
    lowCutRange.setSkewForCentre (200.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::lowCut, "Low Cut", lowCutRange, lowCutMinHz, frequencyAttributes (lowCutMinHz)));

    NormalisableRange<float> highCutRange { highCutMinHz, highCutMaxHz, 1.0f };
    highCutRange.setSkewForCentre (3000.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::highCut, "High Cut", highCutRange, highCutMaxHz, frequencyAttributes (highCutMaxHz)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ID::filterPos, "Filter Position", getFilterPositionNames(), (int) FilterPosition::inFeedbackLoop));

    layout.add (std::make_unique<AudioParameterBool> (ID::freeze, "Freeze", false, onOffAttributes()));
    layout.add (std::make_unique<AudioParameterBool> (ID::pingPong, "Ping-Pong", false, onOffAttributes()));

    NormalisableRange<float> widthRange { 0.0f, maxStereoWidthMs, 1.0f };
    widthRange.setSkewForCentre (20.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::stereoWidthMs, "Stereo Width", widthRange, 0.0f, millisecondAttributes()));

    // ---- Performance ---------------------------------------------------------
    auto gesture = [&layout, &onOffAttributes] (const ParameterID& id, const String& name)
    {
        layout.add (std::make_unique<AudioParameterBool> (id, name, false, onOffAttributes()));
    };

    gesture (ID::throwGesture, "Throw");
    gesture (ID::tapestop,     "Tapestop");
    gesture (ID::runaway,      "Runaway");
    gesture (ID::reverse,      "Reverse");
    gesture (ID::reset,        "Reset");

    layout.add (std::make_unique<AudioParameterChoice> (
        ID::inputMode, "Input", getInputModeNames(), (int) InputMode::always));

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::throwLevelDb, "Throw Level", NormalisableRange<float> { minThrowLevelDb, maxThrowLevelDb, 0.1f }, defaultThrowLevelDb,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return formatDecibels (v, false); })
            .withValueFromStringFunction ([] (const String& text) { return text.getFloatValue(); })));

    NormalisableRange<float> freezeFadeRange { minFreezeFadeMs, maxFreezeFadeMs, 1.0f };
    freezeFadeRange.setSkewForCentre (200.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::freezeFadeMs, "Freeze Fade", freezeFadeRange, defaultFreezeFadeMs, millisecondAttributes()));

    NormalisableRange<float> tapestopRange { minTapestopMs, maxTapestopMs, 1.0f };
    tapestopRange.setSkewForCentre (500.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::tapestopTimeMs, "Tapestop Time", tapestopRange, defaultTapestopMs, millisecondAttributes()));

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::runawayDrive, "Runaway Drive", percentRange, 50.0f, percentAttributes()));

    // ---- Character -----------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (ID::wear, "Wear", percentRange, 0.0f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterFloat> (ID::diffusion, "Diffusion", percentRange, 0.0f, percentAttributes()));

    // ---- Added after release (kept last, in order) ---------------------------
    // The taps themselves aren't parameters: they live in the state tree (Taps.h).
    layout.add (std::make_unique<AudioParameterBool> (ID::multiTap, "Multi-Tap", false, onOffAttributes()));

    layout.add (std::make_unique<AudioParameterChoice> (ID::style, "Style", getStyleNames(), 0));
    layout.add (std::make_unique<AudioParameterFloat> (ID::shimmer, "Shimmer", percentRange, 0.0f, percentAttributes()));

    StringArray pitchNames;
    for (const auto& p : getShimmerPitches())
        pitchNames.add (p.name);

    layout.add (std::make_unique<AudioParameterChoice> (ID::shimmerPitch, "Shimmer Pitch", pitchNames, getDefaultShimmerPitchIndex()));

    layout.add (std::make_unique<AudioParameterChoice> (ID::modType, "Modulation", getModulationTypeNames(), 0));

    NormalisableRange<float> rateRange { minModRateHz, maxModRateHz, 0.01f };
    rateRange.setSkewForCentre (1.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::modRate, "Modulation Rate", rateRange, defaultModRateHz,
        AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return formatHertz (v, false); })
            .withValueFromStringFunction ([] (const String& text) { return text.getFloatValue(); })));

    layout.add (std::make_unique<AudioParameterFloat> (ID::modDepth, "Modulation Depth", percentRange, 50.0f, percentAttributes()));

    layout.add (std::make_unique<AudioParameterChoice> (
        ID::modPos, "Modulation Position", getFilterPositionNames(), (int) FilterPosition::inFeedbackLoop));

    layout.add (std::make_unique<AudioParameterFloat> (
        ID::feelMs, "Feel", NormalisableRange<float> { -maxFeelMs, maxFeelMs, 1.0f }, 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return formatSignedMilliseconds (v, false); })
            .withValueFromStringFunction ([] (const String& text) { return text.getFloatValue(); })));

    layout.add (std::make_unique<AudioParameterFloat> (ID::groove, "Groove", percentRange, 0.0f, percentAttributes()));

    return layout;
}
}
