#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "Taps.h"

namespace
{
    // Fixed MIDI map. Must match docs/perform-map.md.
    constexpr int firstHeldNote  = 36; // 36-40: Throw, Freeze, Tapestop, Runaway, Reverse
    constexpr int resetNote      = 43;
    constexpr int firstLatchNote = 44; // 44-48: same order, each note-on toggles
    constexpr int sustainCC      = 64; // held Freeze
    constexpr int delayTimeCC    = 20; // Delay Time, or Sync Division while synced

    const char* const gestureNames[] { "Throw", "Freeze", "Tapestop", "Runaway", "Reverse" };

    struct CCMapping { int cc; const juce::ParameterID* id; };

    const std::vector<CCMapping>& getCCMap()
    {
        static const std::vector<CCMapping> map {
            { 21, &Params::ID::feedback },     { 22, &Params::ID::mix },
            { 23, &Params::ID::wear },         { 85, &Params::ID::runawayDrive },
            { 24, &Params::ID::diffusion },
            { 31, &Params::ID::tapestopTimeMs },
            { 25, &Params::ID::lowCut },       { 26, &Params::ID::highCut },
            { 27, &Params::ID::stereoWidthMs }, { 28, &Params::ID::timeSmoothingMs },
            { 29, &Params::ID::throwLevelDb }, { 30, &Params::ID::freezeFadeMs },
            { 32, &Params::ID::shimmer },      { 33, &Params::ID::shimmerPitch },
            { 34, &Params::ID::modRate },      { 35, &Params::ID::modDepth },
            { 36, &Params::ID::feelMs },       { 37, &Params::ID::groove },
            { 38, &Params::ID::brandonFeedback }, { 39, &Params::ID::brandonOrbit },
            { 40, &Params::ID::brandonCrush }, { 41, &Params::ID::brandonPitch },
            { 42, &Params::ID::brandonChaos },
            { 102, &Params::ID::sync },        { 103, &Params::ID::pingPong },
            { 104, &Params::ID::inputMode },   { 105, &Params::ID::filterPos },
            { 106, &Params::ID::multiTap },    { 107, &Params::ID::modType },
            { 108, &Params::ID::modPos },      { 109, &Params::ID::style },
            { 110, &Params::ID::brandon },
        };
        return map;
    }

    void setAsCompleteGesture (juce::RangedAudioParameter& param, float normalised)
    {
        param.beginChangeGesture();
        param.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        param.endChangeGesture();
    }
}

EchoFactoryProcessor::EchoFactoryProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EchoFactoryState", Params::createLayout())
{
    delayTimeParam    = apvts.getRawParameterValue (Params::ID::delayTimeMs.getParamID());
    smoothingParam    = apvts.getRawParameterValue (Params::ID::timeSmoothingMs.getParamID());
    syncParam         = apvts.getRawParameterValue (Params::ID::sync.getParamID());
    syncDivisionParam = apvts.getRawParameterValue (Params::ID::syncDivision.getParamID());
    feedbackParam     = apvts.getRawParameterValue (Params::ID::feedback.getParamID());
    mixParam          = apvts.getRawParameterValue (Params::ID::mix.getParamID());
    lowCutParam       = apvts.getRawParameterValue (Params::ID::lowCut.getParamID());
    highCutParam      = apvts.getRawParameterValue (Params::ID::highCut.getParamID());
    filterPosParam    = apvts.getRawParameterValue (Params::ID::filterPos.getParamID());
    pingPongParam     = apvts.getRawParameterValue (Params::ID::pingPong.getParamID());
    widthParam        = apvts.getRawParameterValue (Params::ID::stereoWidthMs.getParamID());
    inputModeParam    = apvts.getRawParameterValue (Params::ID::inputMode.getParamID());
    throwLevelParam   = apvts.getRawParameterValue (Params::ID::throwLevelDb.getParamID());
    freezeFadeParam   = apvts.getRawParameterValue (Params::ID::freezeFadeMs.getParamID());
    runawayDriveParam = apvts.getRawParameterValue (Params::ID::runawayDrive.getParamID());
    tapestopTimeParam = apvts.getRawParameterValue (Params::ID::tapestopTimeMs.getParamID());
    wearParam         = apvts.getRawParameterValue (Params::ID::wear.getParamID());
    diffusionParam    = apvts.getRawParameterValue (Params::ID::diffusion.getParamID());
    multiTapParam     = apvts.getRawParameterValue (Params::ID::multiTap.getParamID());
    styleParam        = apvts.getRawParameterValue (Params::ID::style.getParamID());
    shimmerParam      = apvts.getRawParameterValue (Params::ID::shimmer.getParamID());
    shimmerPitchParam = apvts.getRawParameterValue (Params::ID::shimmerPitch.getParamID());
    modTypeParam      = apvts.getRawParameterValue (Params::ID::modType.getParamID());
    modRateParam      = apvts.getRawParameterValue (Params::ID::modRate.getParamID());
    modDepthParam     = apvts.getRawParameterValue (Params::ID::modDepth.getParamID());
    modPosParam       = apvts.getRawParameterValue (Params::ID::modPos.getParamID());
    feelParam         = apvts.getRawParameterValue (Params::ID::feelMs.getParamID());
    grooveParam       = apvts.getRawParameterValue (Params::ID::groove.getParamID());
    brandonParam         = apvts.getRawParameterValue (Params::ID::brandon.getParamID());
    brandonFeedbackParam = apvts.getRawParameterValue (Params::ID::brandonFeedback.getParamID());
    brandonOrbitParam    = apvts.getRawParameterValue (Params::ID::brandonOrbit.getParamID());
    brandonCrushParam    = apvts.getRawParameterValue (Params::ID::brandonCrush.getParamID());
    brandonPitchParam    = apvts.getRawParameterValue (Params::ID::brandonPitch.getParamID());
    brandonChaosParam    = apvts.getRawParameterValue (Params::ID::brandonChaos.getParamID());

    for (size_t i = 0; i < gestureParams.size(); ++i)
        gestureParams[i] = apvts.getRawParameterValue (Params::getGestureIDs()[i]->getParamID());

    for (auto& cc : pendingCC)
        cc = -1.0f;

    Taps::ensureTree (apvts.state);
    apvts.state.addListener (this);

    apvts.addParameterListener (Params::ID::reset.getParamID(), this);
    startTimerHz (60);
}

EchoFactoryProcessor::~EchoFactoryProcessor()
{
    stopTimer();
    apvts.state.removeListener (this);
    apvts.removeParameterListener (Params::ID::reset.getParamID(), this);
}

void EchoFactoryProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (Taps::ID::tap) || tree.hasType (Taps::ID::taps))
        tapsDirty = true;
}

void EchoFactoryProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (Taps::ID::taps) || child.hasType (Taps::ID::taps))
        tapsDirty = true;
}

void EchoFactoryProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent.hasType (Taps::ID::taps) || child.hasType (Taps::ID::taps))
        tapsDirty = true;
}

void EchoFactoryProcessor::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent.hasType (Taps::ID::taps))
        tapsDirty = true;
}

void EchoFactoryProcessor::valueTreeRedirected (juce::ValueTree&)
{
    tapsDirty = true;
}

void EchoFactoryProcessor::parameterChanged (const juce::String&, float newValue)
{
    if (newValue >= 0.5f)
        resetRequested = true;
}

void EchoFactoryProcessor::timerCallback()
{
    if (tapsDirty.exchange (false))
        engine.setTaps (Taps::toEngineSettings (apvts.state.getChildWithName (Taps::ID::taps)));

    if (resetRequested.exchange (false))
        resetPerformance();

    if (const auto latches = latchRequests.exchange (0); latches != 0)
    {
        for (int i = 0; i < numGestures; ++i)
        {
            if ((latches & (1u << i)) == 0)
                continue;

            auto& param = *apvts.getParameter (Params::getGestureIDs()[(size_t) i]->getParamID());
            const auto nowOn = param.getValue() < 0.5f;
            setAsCompleteGesture (param, nowOn ? 1.0f : 0.0f);

            if (onGestureLatched != nullptr)
                onGestureLatched (gestureNames[i], nowOn);
        }
    }

    if (const auto value = pendingCC[delayTimeCC].exchange (-1.0f); value >= 0.0f)
    {
        const auto& id = syncParam->load() >= 0.5f ? Params::ID::syncDivision : Params::ID::delayTimeMs;
        setAsCompleteGesture (*apvts.getParameter (id.getParamID()), value);
    }

    for (const auto& mapping : getCCMap())
        if (const auto value = pendingCC[(size_t) mapping.cc].exchange (-1.0f); value >= 0.0f)
            setAsCompleteGesture (*apvts.getParameter (mapping.id->getParamID()), value);
}

void EchoFactoryProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const auto note = message.getNoteNumber();

            if (juce::isPositiveAndBelow (note - firstHeldNote, numGestures))
                midiNoteHeld[(size_t) (note - firstHeldNote)] = true;
            else if (juce::isPositiveAndBelow (note - firstLatchNote, numGestures))
                latchRequests.fetch_or (1u << (note - firstLatchNote));
            else if (note == resetNote)
                resetRequested = true;
        }
        else if (message.isNoteOff())
        {
            const auto note = message.getNoteNumber();

            if (juce::isPositiveAndBelow (note - firstHeldNote, numGestures))
                midiNoteHeld[(size_t) (note - firstHeldNote)] = false;
        }
        else if (message.isController())
        {
            const auto cc = message.getControllerNumber();
            const auto value = message.getControllerValue();

            if (cc == sustainCC)
                sustainHeld = value >= 64;
            else
                pendingCC[(size_t) cc] = (float) value / 127.0f; // unmapped CCs are never read
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (auto& held : midiNoteHeld)
                held = false;

            sustainHeld = false;
        }
    }
}

bool EchoFactoryProcessor::isGestureOn (int index) const
{
    return gestureParams[(size_t) index]->load() >= 0.5f
        || midiNoteHeld[(size_t) index]
        || (index == 1 && sustainHeld);
}

bool EchoFactoryProcessor::isGestureHeldByMidi (const juce::String& paramID) const
{
    for (int i = 0; i < numGestures; ++i)
        if (Params::getGestureIDs()[(size_t) i]->getParamID() == paramID)
            return midiNoteHeld[(size_t) i] || (i == 1 && sustainHeld);

    return false;
}

void EchoFactoryProcessor::nudgeFeedback (float deltaPercent)
{
    auto& param = *apvts.getParameter (Params::ID::feedback.getParamID());

    if (! feedbackBaseline.has_value())
        feedbackBaseline = param.getValue();

    const auto percent = param.convertFrom0to1 (param.getValue()) + deltaPercent;
    setAsCompleteGesture (param, param.convertTo0to1 (param.getNormalisableRange().snapToLegalValue (percent)));
}

void EchoFactoryProcessor::nudgeDelayTime (int direction)
{
    if (syncParam->load() >= 0.5f)
    {
        auto& param = *apvts.getParameter (Params::ID::syncDivision.getParamID());

        if (! syncDivisionBaseline.has_value())
            syncDivisionBaseline = param.getValue();

        const auto index = juce::roundToInt (param.convertFrom0to1 (param.getValue())) + direction;
        setAsCompleteGesture (param, param.convertTo0to1 ((float) juce::jlimit (0, (int) Params::getSyncDivisions().size() - 1, index)));
    }
    else
    {
        auto& param = *apvts.getParameter (Params::ID::delayTimeMs.getParamID());

        if (! delayTimeBaseline.has_value())
            delayTimeBaseline = param.getValue();

        // Same step as the Delay Time knob's arrow keys: 1/100 of the skewed range.
        setAsCompleteGesture (param, param.getValue() + 0.01f * (float) direction);
    }
}

void EchoFactoryProcessor::resetPerformance()
{
    for (const auto* id : { &Params::ID::throwGesture, &Params::ID::freeze, &Params::ID::tapestop,
                            &Params::ID::runaway, &Params::ID::reverse, &Params::ID::reset })
    {
        auto* param = apvts.getParameter (id->getParamID());

        if (param->getValue() >= 0.5f)
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (0.0f);
            param->endChangeGesture();
        }
    }

    resetRequested = false; // the Reset parameter itself has just been released

    for (auto& held : midiNoteHeld)
        held = false;

    sustainHeld = false;

    // Undo Perform-mode arrow-key nudges.
    const std::pair<std::optional<float>*, const juce::ParameterID*> baselines[] {
        { &feedbackBaseline, &Params::ID::feedback },
        { &delayTimeBaseline, &Params::ID::delayTimeMs },
        { &syncDivisionBaseline, &Params::ID::syncDivision },
    };

    for (auto [baseline, id] : baselines)
    {
        if (baseline->has_value())
            setAsCompleteGesture (*apvts.getParameter (id->getParamID()), **baseline);

        baseline->reset();
    }

    if (onPerformanceReset != nullptr)
        onPerformanceReset();
}

bool EchoFactoryProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void EchoFactoryProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels(), Params::maxDelayMs, Params::maxStereoWidthMs);
    updateEngineParameters();
    engine.reset(); // jump straight to the current settings, no glide on start-up
}

void EchoFactoryProcessor::reset()
{
    engine.reset();
}

float EchoFactoryProcessor::getTargetDelayMs() const
{
    if (syncParam->load() < 0.5f)
        return delayTimeParam->load();

    // Feel pushes synced echoes ahead of or behind the beat.
    return juce::jlimit (Params::minDelayMs, Params::maxDelayMs,
                         Params::divisionToMs (juce::roundToInt (syncDivisionParam->load()), hostBpm) + feelParam->load());
}

void EchoFactoryProcessor::updateEngineParameters()
{
    engine.setDelaySmoothingMs (smoothingParam->load());
    engine.setDelayMs (getTargetDelayMs());
    engine.setFeedback (feedbackParam->load() * 0.01f);
    engine.setMix (mixParam->load() * 0.01f);

    // The range extremes mean "Off" (0 bypasses the filter).
    const auto lowCut  = lowCutParam->load();
    const auto highCut = highCutParam->load();
    engine.setLowCutHz  (lowCut  <= Params::lowCutMinHz  ? 0.0f : lowCut);
    engine.setHighCutHz (highCut >= Params::highCutMaxHz ? 0.0f : highCut);
    engine.setFiltersInFeedbackLoop (juce::roundToInt (filterPosParam->load()) == (int) Params::FilterPosition::inFeedbackLoop);
    engine.setFreezeFadeMs (freezeFadeParam->load());
    engine.setFrozen (isGestureOn (1));

    // Throw sends the input in at Throw Level; otherwise it goes in at unity,
    // or not at all in Throw Only mode.
    const auto throwing  = isGestureOn (0);
    const auto throwOnly = juce::roundToInt (inputModeParam->load()) == (int) Params::InputMode::throwOnly;
    engine.setInputSend (throwing ? juce::Decibels::decibelsToGain (throwLevelParam->load())
                                  : throwOnly ? 0.0f : 1.0f);
    engine.setPingPong (pingPongParam->load() >= 0.5f);
    engine.setStereoWidthMs (widthParam->load());
    engine.setWear (wearParam->load() * 0.01f);
    engine.setStyle (juce::roundToInt (styleParam->load()));
    engine.setRunaway (isGestureOn (3));
    engine.setRunawayDrive (runawayDriveParam->load() * 0.01f);
    engine.setTapestopTimeMs (tapestopTimeParam->load());
    engine.setTapestop (isGestureOn (2));
    engine.setReverse (isGestureOn (4));
    engine.setDiffusion (diffusionParam->load() * 0.01f);
    engine.setShimmer (shimmerParam->load() * 0.01f);

    const auto& pitches = Params::getShimmerPitches();
    const auto pitchIndex = juce::jlimit (0, (int) pitches.size() - 1, juce::roundToInt (shimmerPitchParam->load()));
    engine.setShimmerSemitones ((float) pitches[(size_t) pitchIndex].semitones);

    engine.setModulationType (juce::roundToInt (modTypeParam->load()));
    engine.setModulationRateHz (modRateParam->load());
    engine.setModulationDepth (modDepthParam->load() * 0.01f);
    engine.setModulationInFeedbackLoop (juce::roundToInt (modPosParam->load()) == (int) Params::FilterPosition::inFeedbackLoop);

    engine.setMultiTap (multiTapParam->load() >= 0.5f);
    engine.setTapTempo (syncParam->load() >= 0.5f, hostBpm);
    engine.setGroove (grooveParam->load() * 0.01f);
    engine.setFeelMs (feelParam->load());

    engine.setBrandon (brandonParam->load() >= 0.5f);
    engine.setBrandonFeedback (brandonFeedbackParam->load() * 0.01f);
    engine.setBrandonOrbit (brandonOrbitParam->load() * 0.01f);
    engine.setBrandonCrush (brandonCrushParam->load() * 0.01f);
    engine.setBrandonPitch (brandonPitchParam->load() * 0.01f);
    engine.setBrandonChaos (brandonChaosParam->load() * 0.01f);
}

void EchoFactoryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;

    handleMidi (midi);
    updateEngineParameters();
    engine.process (buffer);
}

juce::AudioProcessorEditor* EchoFactoryProcessor::createEditor()
{
    return new EchoFactoryEditor (*this);
}

void EchoFactoryProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EchoFactoryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            Taps::ensureTree (state); // sessions from before Multi-Tap
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EchoFactoryProcessor();
}
