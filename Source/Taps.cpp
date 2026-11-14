#include "Taps.h"
#include "Parameters.h"

namespace Taps
{
juce::NormalisableRange<float> getLevelRange()
{
    juce::NormalisableRange<float> range { minLevelDb, maxLevelDb, 0.1f };
    range.setSkewForCentre (-12.0f);
    return range;
}

juce::NormalisableRange<float> getPanRange()   { return { -maxPan, maxPan, 1.0f }; }
juce::NormalisableRange<float> getPitchRange() { return { -maxPitchSemitones, maxPitchSemitones, 1.0f }; }

juce::String formatLevel (float db, bool spoken)
{
    if (db <= minLevelDb)
        return "Off";

    return Params::formatDecibels (db, spoken);
}

juce::String formatPan (float pan, bool spoken)
{
    const auto amount = juce::roundToInt (std::abs (pan));

    if (amount == 0)
        return spoken ? "centre" : "C";

    const auto side = pan < 0.0f ? (spoken ? "left" : "L") : (spoken ? "right" : "R");
    return spoken ? juce::String (amount) + " percent " + side
                  : juce::String (amount) + " " + side;
}

juce::String formatSemitones (float semitones, bool spoken)
{
    return Params::formatSemitones (juce::roundToInt (semitones), spoken);
}

juce::ValueTree makeTap (float timeMs, int division, float levelDb, float pan, int pitch, bool reverse)
{
    juce::ValueTree tap (ID::tap);
    tap.setProperty (ID::uid, juce::Random::getSystemRandom().nextInt (juce::Range<int> (1, std::numeric_limits<int>::max())), nullptr);
    tap.setProperty (ID::timeMs, Params::getDelayTimeRange().snapToLegalValue (timeMs), nullptr);
    tap.setProperty (ID::division, juce::jlimit (0, (int) Params::getSyncDivisions().size() - 1, division), nullptr);
    tap.setProperty (ID::levelDb, getLevelRange().snapToLegalValue (levelDb), nullptr);
    tap.setProperty (ID::pan, getPanRange().snapToLegalValue (pan), nullptr);
    tap.setProperty (ID::pitch, juce::jlimit (-(int) maxPitchSemitones, (int) maxPitchSemitones, pitch), nullptr);
    tap.setProperty (ID::reverse, reverse, nullptr);
    return tap;
}

juce::ValueTree makeNextTap (const juce::ValueTree& taps)
{
    const auto count = taps.getNumChildren();

    if (count == 0)
        return makeTap();

    const auto last = taps.getChild (count - 1);
    const auto lastMs = (float) last.getProperty (ID::timeMs, defaultTimeMs);
    const auto lastDivision = (int) last.getProperty (ID::division, defaultDivision);

    // Continue the spacing of the last two taps, or step on by 125 ms / one division.
    auto msStep = 125.0f;
    auto divisionStep = 1;

    if (count >= 2)
    {
        const auto previous = taps.getChild (count - 2);
        const auto msGap = lastMs - (float) previous.getProperty (ID::timeMs, defaultTimeMs);
        const auto divisionGap = lastDivision - (int) previous.getProperty (ID::division, defaultDivision);

        if (msGap > 0.0f)     msStep = msGap;
        if (divisionGap > 0)  divisionStep = divisionGap;
    }

    return makeTap (lastMs + msStep, lastDivision + divisionStep,
                    (float) last.getProperty (ID::levelDb, defaultLevelDb),
                    (float) last.getProperty (ID::pan, 0.0f),
                    (int) last.getProperty (ID::pitch, 0));
}

void ensureTree (juce::ValueTree& state)
{
    if (auto taps = state.getChildWithName (ID::taps); taps.isValid())
    {
        // A hand-edited preset could have more than the limit.
        while (taps.getNumChildren() > maxTaps)
            taps.removeChild (taps.getNumChildren() - 1, nullptr);

        return;
    }

    juce::ValueTree taps (ID::taps);
    taps.appendChild (makeTap(), nullptr);
    state.appendChild (taps, nullptr);
}

std::vector<DelayEngine::TapSettings> toEngineSettings (const juce::ValueTree& taps)
{
    std::vector<DelayEngine::TapSettings> settings;
    const auto& divisions = Params::getSyncDivisions();

    for (const auto& tap : taps)
    {
        if (! tap.hasType (ID::tap))
            continue;

        if ((int) settings.size() == maxTaps)
            break;

        DelayEngine::TapSettings s;

        // 0 means "no tap" to the engine; a tap saved without a uid gets one from its position.
        const auto uid = (int) tap.getProperty (ID::uid, 0);
        s.uid = uid != 0 ? uid : -(int) settings.size() - 1;
        s.delayMs = (float) tap.getProperty (ID::timeMs, defaultTimeMs);

        const auto division = juce::jlimit (0, (int) divisions.size() - 1, (int) tap.getProperty (ID::division, defaultDivision));
        s.beats = (float) divisions[(size_t) division].beats;

        const auto db = (float) tap.getProperty (ID::levelDb, defaultLevelDb);
        s.gain = db <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (db);
        s.pan = juce::jlimit (-1.0f, 1.0f, (float) tap.getProperty (ID::pan, 0.0f) / maxPan);
        s.semitones = (float) (int) tap.getProperty (ID::pitch, 0);
        s.reverse = (bool) tap.getProperty (ID::reverse, false);
        settings.push_back (s);
    }

    return settings;
}
}
