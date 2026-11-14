#include "PresetManager.h"
#include "Parameters.h"
#include "Taps.h"

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    using namespace Params::ID;

    // Sync divisions by index into Params::getSyncDivisions().
    constexpr float sixteenth = 4, eighth = 7, dottedEighth = 8, quarter = 10, dottedQuarter = 11, half = 13;
    constexpr int tapSixteenth = 4, tapEighth = 7, tapDottedEighth = 8, tapQuarter = 10, tapDottedQuarter = 11;
    constexpr float on = 1, throwOnly = (float) Params::InputMode::throwOnly, outputOnly = (float) Params::FilterPosition::outputOnly;

    // Styles by index into Params::getStyleNames(), modulation types into
    // getModulationTypeNames(), shimmer pitches into getShimmerPitches().
    constexpr float tapeEcho = 1, bucketBrigade = 2, loFiDigital = 3, telephone = 4;
    constexpr float chorus = 1, flanger = 2, phaser = 3;
    constexpr float octaveUp = 6, octaveAndFifthUp = 7;

    addFactoryPreset ("Init", "Every control at its default.", {});

    addFactoryPreset ("Clean Slapback", "One short repeat.",
                      { { &delayTimeMs, 110 }, { &feedback, 10 }, { &mix, 25 } });

    addFactoryPreset ("Quarter Note Echo", "Repeats locked to the host tempo.",
                      { { &sync, on }, { &syncDivision, quarter }, { &feedback, 40 }, { &mix, 30 }, { &highCut, 9000 } });

    addFactoryPreset ("Dotted Ping-Pong", "Dotted eighths bouncing left and right.",
                      { { &sync, on }, { &syncDivision, dottedEighth }, { &pingPong, on }, { &feedback, 45 }, { &mix, 30 },
                        { &stereoWidthMs, 12 }, { &highCut, 7000 } });

    addFactoryPreset ("Dub Throw", "Only thrown audio echoes. Hold F to throw.",
                      { { &sync, on }, { &syncDivision, dottedQuarter }, { &inputMode, throwOnly }, { &throwLevelDb, 3 },
                        { &feedback, 65 }, { &mix, 45 }, { &pingPong, on }, { &lowCut, 250 }, { &highCut, 2500 }, { &wear, 30 } });

    addFactoryPreset ("Worn Tape", "Saturated, wobbly repeats. Left and right arrows bend the tape.",
                      { { &delayTimeMs, 380 }, { &timeSmoothingMs, 300 }, { &feedback, 50 }, { &mix, 35 },
                        { &wear, 60 }, { &highCut, 5000 } });

    addFactoryPreset ("Glide Machine", "Long glides between delay times. Use left and right arrows.",
                      { { &delayTimeMs, 300 }, { &timeSmoothingMs, 1500 }, { &feedback, 55 }, { &mix, 40 }, { &wear, 20 } });

    addFactoryPreset ("Lo-Fi Radio", "Narrow, crunchy echoes.",
                      { { &delayTimeMs, 180 }, { &feedback, 40 }, { &mix, 40 }, { &lowCut, 600 }, { &highCut, 2200 }, { &wear, 80 } });

    addFactoryPreset ("Tape Stop Groove", "Hold S to stop the tape, release to spin it back up.",
                      { { &sync, on }, { &syncDivision, eighth }, { &feedback, 45 }, { &mix, 40 }, { &wear, 25 },
                        { &tapestopTimeMs, 400 } });

    addFactoryPreset ("Runaway Wall", "Hold A to let the feedback run away.",
                      { { &delayTimeMs, 250 }, { &feedback, 55 }, { &mix, 40 }, { &wear, 40 }, { &runawayDrive, 70 },
                        { &lowCut, 150 }, { &highCut, 6000 } });

    addFactoryPreset ("Stutter Freeze", "Tap D for instant stutters.",
                      { { &sync, on }, { &syncDivision, sixteenth }, { &feedback, 30 }, { &mix, 50 }, { &freezeFadeMs, 10 } });

    addFactoryPreset ("Backwards Memories", "Hold G to play the echoes backwards.",
                      { { &sync, on }, { &syncDivision, half }, { &feedback, 40 }, { &mix, 45 }, { &diffusion, 30 }, { &highCut, 8000 } });

    addFactoryPreset ("Echoes in a Hall", "Echoes with a reverb tail.",
                      { { &sync, on }, { &syncDivision, quarter }, { &pingPong, on }, { &feedback, 50 }, { &mix, 35 },
                        { &diffusion, 55 }, { &highCut, 9000 } });

    addFactoryPreset ("Ambient Wash", "A reverb with no separate repeats. Feedback sets its length.",
                      { { &delayTimeMs, 450 }, { &feedback, 75 }, { &mix, 40 }, { &diffusion, 100 }, { &stereoWidthMs, 20 },
                        { &highCut, 8000 } });

    addFactoryPreset ("Frozen Pad", "Shift and D latches a slowly swelling freeze. Backspace releases it.",
                      { { &delayTimeMs, 600 }, { &feedback, 60 }, { &mix, 50 }, { &diffusion, 80 }, { &freezeFadeMs, 1500 },
                        { &lowCut, 120 } });

    addFactoryPreset ("Multi-Tap Rhythm", "Four taps across the stereo field. Try reversing one.",
                      { { &sync, on }, { &syncDivision, quarter }, { &multiTap, on }, { &feedback, 30 }, { &mix, 40 },
                        { &highCut, 9000 } },
                      { Taps::makeTap (125, tapSixteenth, -3, -60),
                        Taps::makeTap (250, tapEighth, -3, 60),
                        Taps::makeTap (375, tapDottedEighth, -6, -30),
                        Taps::makeTap (750, tapDottedQuarter, -6, 30, 12) });

    addFactoryPreset ("Swung Sixteenths", "Four taps a beat with a shuffle. Groove sets the swing.",
                      { { &sync, on }, { &syncDivision, quarter }, { &multiTap, on }, { &groove, 66 }, { &feedback, 35 },
                        { &mix, 40 }, { &highCut, 8000 } },
                      { Taps::makeTap (125, tapSixteenth, -3, -40),
                        Taps::makeTap (250, tapEighth, -3, 40),
                        Taps::makeTap (375, tapDottedEighth, -6, -20),
                        Taps::makeTap (500, tapQuarter, -2, 20) });

    addFactoryPreset ("Shimmer Cathedral", "An octave-up reverb that blooms. Hold D to freeze it.",
                      { { &delayTimeMs, 500 }, { &feedback, 70 }, { &mix, 45 }, { &diffusion, 100 }, { &shimmer, 70 },
                        { &shimmerPitch, octaveUp }, { &stereoWidthMs, 25 }, { &lowCut, 100 }, { &highCut, 10000 } });

    addFactoryPreset ("Fifths Shimmer", "A reverb climbing in fifths and octaves behind half-note echoes.",
                      { { &sync, on }, { &syncDivision, half }, { &feedback, 60 }, { &mix, 40 }, { &diffusion, 85 },
                        { &shimmer, 50 }, { &shimmerPitch, octaveAndFifthUp }, { &highCut, 9000 } });

    addFactoryPreset ("Chorus Repeats", "Each repeat picks up more chorus.",
                      { { &delayTimeMs, 380 }, { &feedback, 50 }, { &mix, 35 }, { &modType, chorus }, { &modRate, 0.5f },
                        { &modDepth, 60 }, { &highCut, 8000 } });

    addFactoryPreset ("Phaser Trails", "Dotted eighths that swirl deeper with every repeat.",
                      { { &sync, on }, { &syncDivision, dottedEighth }, { &feedback, 55 }, { &mix, 35 }, { &modType, phaser },
                        { &modRate, 0.25f }, { &modDepth, 75 } });

    addFactoryPreset ("Flanged Ping-Pong", "Flanged echoes bouncing left and right. Shift and G latches reverse.",
                      { { &sync, on }, { &syncDivision, eighth }, { &pingPong, on }, { &feedback, 45 }, { &mix, 35 },
                        { &modType, flanger }, { &modRate, 0.15f }, { &modDepth, 80 }, { &modPos, outputOnly } });

    addFactoryPreset ("Tape Echo Unit", "Bright, fluttery tape echo. Hold A to let it run away.",
                      { { &delayTimeMs, 420 }, { &feedback, 50 }, { &mix, 35 }, { &style, tapeEcho }, { &wear, 55 },
                        { &runawayDrive, 60 } });

    addFactoryPreset ("Bucket Brigade", "Dark analog repeats that melt away. Left and right arrows bend the time.",
                      { { &delayTimeMs, 320 }, { &timeSmoothingMs, 400 }, { &feedback, 55 }, { &mix, 35 },
                        { &style, bucketBrigade }, { &wear, 60 } });

    addFactoryPreset ("Lo-Fi Crush", "Eight-bit repeats that crumble.",
                      { { &delayTimeMs, 250 }, { &feedback, 45 }, { &mix, 40 }, { &style, loFiDigital }, { &wear, 70 } });

    addFactoryPreset ("Telephone Line", "Narrow, clipped repeats.",
                      { { &delayTimeMs, 180 }, { &feedback, 50 }, { &mix, 40 }, { &style, telephone }, { &wear, 65 } });
}

void PresetManager::addFactoryPreset (const juce::String& name, const juce::String& hint,
                                      std::initializer_list<std::pair<const juce::ParameterID*, float>> values,
                                      std::initializer_list<juce::ValueTree> taps)
{
    juce::ValueTree state (apvts.state.getType());

    for (const auto& [id, value] : values)
    {
        auto* param = apvts.getParameter (id->getParamID());
        jassert (param != nullptr);

        juce::ValueTree child ("PARAM");
        child.setProperty ("id", id->getParamID(), nullptr);
        child.setProperty ("value", param->getNormalisableRange().snapToLegalValue (value), nullptr);
        state.appendChild (child, nullptr);
    }

    if (taps.size() > 0)
    {
        juce::ValueTree tapsTree (Taps::ID::taps);

        for (const auto& tap : taps)
            tapsTree.appendChild (tap, nullptr);

        state.appendChild (tapsTree, nullptr);
    }

    factoryPresets.push_back ({ name, hint, state.toXmlString() });
}

juce::File PresetManager::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("ZBAudio")
               .getChildFile ("Echo Factory")
               .getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::getUserPresets() const
{
    auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*" + fileExtension);

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });

    return files;
}

bool PresetManager::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factoryPresets.size()))
        return false;

    const auto& preset = factoryPresets[(size_t) index];

    if (auto xml = juce::parseXML (preset.xml))
        return applyState (juce::ValueTree::fromXml (*xml), preset.name);

    return false;
}

bool PresetManager::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
        return applyState (juce::ValueTree::fromXml (*xml), file.getFileNameWithoutExtension());

    return false;
}

bool PresetManager::applyState (const juce::ValueTree& newState, const juce::String& name)
{
    if (! newState.hasType (apvts.state.getType()))
        return false;

    auto stateToLoad = newState.createCopy();

    // Presets saved before a parameter existed would otherwise leave it at
    // whatever value it currently has; reset those to their defaults instead.
    for (auto* p : apvts.processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (! stateToLoad.getChildWithProperty ("id", ranged->getParameterID()).isValid())
            {
                juce::ValueTree child ("PARAM");
                child.setProperty ("id", ranged->getParameterID(), nullptr);
                child.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
                stateToLoad.appendChild (child, nullptr);
            }
        }
    }

    // Presets never load mid-gesture: a preset saved while frozen or throwing
    // would otherwise come back latched. Host sessions still restore as saved.
    for (const auto* id : Params::getGestureIDs())
        stateToLoad.getChildWithProperty ("id", id->getParamID()).setProperty ("value", 0.0f, nullptr);

    Taps::ensureTree (stateToLoad); // presets from before Multi-Tap get one default tap
    apvts.replaceState (stateToLoad);
    setCurrentPresetName (name);
    return true;
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).trim();
}

juce::File PresetManager::getUserPresetFile (const juce::String& name) const
{
    return getUserPresetDirectory().getChildFile (sanitiseName (name) + fileExtension);
}

PresetManager::SaveResult PresetManager::saveUserPreset (const juce::String& name, bool overwrite)
{
    const auto cleanName = sanitiseName (name);

    if (cleanName.isEmpty())
        return SaveResult::invalidName;

    const auto file = getUserPresetFile (cleanName);

    if (file.existsAsFile() && ! overwrite)
        return SaveResult::alreadyExists;

    if (! getUserPresetDirectory().createDirectory())
        return SaveResult::writeFailed;

    auto state = apvts.copyState();
    state.setProperty (presetNameProperty, cleanName, nullptr);

    const auto xml = state.createXml();

    if (xml == nullptr || ! xml->writeTo (file))
        return SaveResult::writeFailed;

    setCurrentPresetName (cleanName);
    return SaveResult::ok;
}

juce::String PresetManager::getCurrentPresetName() const
{
    return apvts.state.getProperty (presetNameProperty, "Default").toString();
}

void PresetManager::setCurrentPresetName (const juce::String& name)
{
    apvts.state.setProperty (presetNameProperty, name, nullptr);
}
