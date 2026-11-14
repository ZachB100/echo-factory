#include "PluginEditor.h"
#include "Parameters.h"
#include "ui/TimeKnob.h"

namespace Colours
{
    const juce::Colour background { 0xff1a1d22 };
    const juce::Colour topBar     { 0xff23272e };
    const juce::Colour text       { 0xfff2f4f7 };
    const juce::Colour accent     { 0xff4fc3f7 };
    const juce::Colour focus      { 0xffffd166 };
}

EchoFactoryEditor::EchoFactoryEditor (EchoFactoryProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), presetBar (p.presetManager), performPad (p.apvts), tapsPanel (p.apvts)
{
    setTitle ("Echo Factory");
    setDescription ("Echo Factory delay by ZBAudio");
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    // Host/standalone traversal stops at a keyboard focus container. It must
    // itself accept focus so keyboard-only entry can reach the controls.
    setWantsKeyboardFocus (true);

    auto& lf = getLookAndFeel();
    lf.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    lf.setColour (juce::Slider::thumbColourId, Colours::accent);
    lf.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    lf.setColour (juce::Label::textColourId, Colours::text);
    lf.setColour (juce::ToggleButton::textColourId, Colours::text);
    lf.setColour (juce::ToggleButton::tickColourId, Colours::accent);

    // ---- Top bar ---------------------------------------------------------
    presetBar.setExplicitFocusOrder (1);
    presetBar.onSaveRequested = [this] { showSaveDialog(); };
    addAndMakeVisible (presetBar);

    // ---- Screen-reader groups -------------------------------------------
    // Focus order numbers inside a group are local to it.
    // Taps come straight after the Delay group, whose Multi-Tap switch shows them.
    delayGroup.setExplicitFocusOrder (3);
    tapsPanel.setExplicitFocusOrder (4);
    characterGroup.setExplicitFocusOrder (5);
    filtersGroup.setExplicitFocusOrder (6);
    modulationGroup.setExplicitFocusOrder (7);
    gestureGroup.setExplicitFocusOrder (8);
    modesGroup.setExplicitFocusOrder (3); // inside the Delay group, between Smoothing and Feedback

    for (auto* group : { &delayGroup, &characterGroup, &filtersGroup, &modulationGroup, &gestureGroup })
        addAndMakeVisible (group);

    addChildComponent (tapsPanel); // shown by Multi-Tap

    delayGroup.addAndMakeVisible (modesGroup);

    // ---- Perform pad -----------------------------------------------------
    // First after the preset bar, so it's one Tab from the top.
    performPad.setTitle ("Perform mode");
    performPad.setDescription ("Hold F to throw, D to freeze, S for tapestop, A for runaway, G for reverse. "
                               "Shift plus a key latches it; press the key again to release. "
                               "Up and down arrows change feedback, left and right arrows change delay time. "
                               "Backspace resets. Escape returns to the controls.");
    performPad.setExplicitFocusOrder (2);
    performPad.onReset = [this] { processorRef.resetPerformance(); };
    performPad.onNudgeFeedback = [this] (float delta) { processorRef.nudgeFeedback (delta); };
    performPad.onNudgeTime     = [this] (int direction) { processorRef.nudgeDelayTime (direction); };
    performPad.isHeldExternally = [this] (const juce::String& id) { return processorRef.isGestureHeldByMidi (id); };
    performPad.onExit  = [this]
    {
        juce::AccessibilityHandler::postAnnouncement ("Edit mode", juce::AccessibilityHandler::AnnouncementPriority::high);
        timeSlider.grabKeyboardFocus();
    };
    addAndMakeVisible (performPad);

    // ---- Delay time / division ------------------------------------------
    timeLabel.attachToComponent (&timeSlider, false);
    timeLabel.setAccessible (false); // the knob's title already says it
    timeLabel.setJustificationType (juce::Justification::centred);
    timeSlider.setExplicitFocusOrder (1);
    delayGroup.addAndMakeVisible (timeSlider);

    // ---- Time smoothing --------------------------------------------------
    setupSlider (delayGroup, smoothingSlider, smoothingLabel, "Smoothing",
                 "How long the delay time takes to glide to a new setting, from 0 milliseconds (instant) to 2 seconds. "
                 "Longer settings give a smoother, tape-like pitch bend.");
    smoothingSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
    smoothingSlider.setExplicitFocusOrder (2);
    smoothingAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::timeSmoothingMs.getParamID(), smoothingSlider);

    // ---- Sync ------------------------------------------------------------
    syncButton.setTitle ("Sync to host tempo");
    syncButton.setDescription ("When on, the delay time follows the host tempo as a note division.");
    syncButton.setWantsKeyboardFocus (true);
    syncButton.setExplicitFocusOrder (1);
    modesGroup.addAndMakeVisible (syncButton);
    syncButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::sync.getParamID(), syncButton);

    // ---- Ping-pong -------------------------------------------------------
    pingPongButton.setTitle ("Ping-Pong");
    pingPongButton.setDescription ("When on, echoes bounce between left and right, starting on the left. "
                                   "With feedback at zero you hear two repeats, one left and one right. Stereo only.");
    pingPongButton.setWantsKeyboardFocus (true);
    pingPongButton.setExplicitFocusOrder (2);
    modesGroup.addAndMakeVisible (pingPongButton);
    pingPongButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::pingPong.getParamID(), pingPongButton);

    // ---- Freeze ----------------------------------------------------------
    freezeButton.setTitle ("Freeze");
    freezeButton.setDescription ("When on, the delay stops taking in new audio and repeats what is in the buffer "
                                 "forever without fading. The dry signal still passes through.");
    freezeButton.setWantsKeyboardFocus (true);
    freezeButton.setExplicitFocusOrder (3);
    modesGroup.addAndMakeVisible (freezeButton);
    freezeButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::freeze.getParamID(), freezeButton);

    // ---- Multi-tap -------------------------------------------------------
    multiTapButton.setTitle ("Multi-Tap");
    multiTapButton.setDescription ("When on, the main echo is replaced by up to 16 taps, each with its own time, level, pan, "
                                   "pitch and reverse. Delay Time and Feedback still set how often the taps repeat; "
                                   "with feedback at zero each tap plays once. The Taps panel follows the Delay controls.");
    multiTapButton.setWantsKeyboardFocus (true);
    multiTapButton.setExplicitFocusOrder (4);
    multiTapButton.onClick = [this]
    {
        if (multiTapButton.getToggleState())
            juce::AccessibilityHandler::postAnnouncement ("Taps shown, after the Delay controls",
                                                          juce::AccessibilityHandler::AnnouncementPriority::medium);
    };
    modesGroup.addAndMakeVisible (multiTapButton);
    multiTapButtonAttachment = std::make_unique<ButtonAttachment> (p.apvts, Params::ID::multiTap.getParamID(), multiTapButton);

    // Follows the sync parameter whether it's changed here, by automation or by a preset.
    syncWatcher = std::make_unique<juce::ParameterAttachment> (
        *p.apvts.getParameter (Params::ID::sync.getParamID()),
        [this] (float value)
        {
            updateTimeControlForSync (value >= 0.5f);
            tapsPanel.setSynced (value >= 0.5f);
        });
    syncWatcher->sendInitialUpdate();

    // ---- Feedback / Mix --------------------------------------------------
    setupPercentSlider (delayGroup, feedbackSlider, feedbackLabel, "Feedback", "Amount of the echo fed back into the delay, 0 to 95 percent.");
    feedbackSlider.setExplicitFocusOrder (4);
    feedbackAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::feedback.getParamID(), feedbackSlider);

    setupPercentSlider (delayGroup, mixSlider, mixLabel, "Mix", "Balance between the dry and delayed signal, 0 to 100 percent.");
    mixSlider.setExplicitFocusOrder (5);
    mixAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::mix.getParamID(), mixSlider);

    // ---- Feel / Groove ---------------------------------------------------
    setupSlider (delayGroup, feelSlider, feelLabel, "Feel",
                 "Moves synced echoes and taps ahead of or behind the beat, minus 50 to plus 50 milliseconds. Only while synced.");
    feelSlider.spokenTextFromValue = [] (double v) { return Params::formatSignedMilliseconds ((float) v, true); };
    feelSlider.setDoubleClickReturnValue (true, 0.0);
    feelSlider.setExplicitFocusOrder (6);
    feelAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::feelMs.getParamID(), feelSlider);

    setupPercentSlider (delayGroup, grooveSlider, grooveLabel, "Groove",
                        "Swing on synced taps, 0 to 100 percent. At full, an off-beat eighth moves to the triplet. "
                        "Only while synced, with Multi-Tap on.");
    grooveSlider.setExplicitFocusOrder (7);
    grooveAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::groove.getParamID(), grooveSlider);

    // ---- Style -----------------------------------------------------------
    setupComboBox (characterGroup, styleBox, styleLabel, "Style",
                   "What Wear sounds like: Studio Tape, Tape Echo, Bucket Brigade, Lo-Fi Digital or Telephone.",
                   Params::getStyleNames());
    styleBox.setExplicitFocusOrder (1);
    styleAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::style.getParamID(), styleBox);

    // ---- Wear ------------------------------------------------------------
    setupPercentSlider (characterGroup, wearSlider, wearLabel, "Wear",
                        "Wear, 0 to 100 percent: saturation, tone and wobble in the chosen Style, "
                        "building up with each repeat. At 0 the delay is clean.");
    wearSlider.setExplicitFocusOrder (2);
    wearAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::wear.getParamID(), wearSlider);

    // ---- Diffusion -------------------------------------------------------
    setupPercentSlider (characterGroup, diffusionSlider, diffusionLabel, "Diffusion",
                        "Turns the echoes into a reverb, 0 to 100 percent. At full there are no separate repeats, "
                        "and Feedback sets how long the reverb lasts. Combine with Freeze for pads. At 0 the echoes are clean.");
    diffusionSlider.setExplicitFocusOrder (3);
    diffusionAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::diffusion.getParamID(), diffusionSlider);

    // ---- Shimmer ---------------------------------------------------------
    setupPercentSlider (characterGroup, shimmerSlider, shimmerLabel, "Shimmer",
                        "Feeds the Diffusion reverb back into itself through a pitch shifter, 0 to 100 percent, "
                        "so the tail climbs by Shimmer Pitch on every pass. Needs Diffusion above 0.");
    shimmerSlider.setExplicitFocusOrder (4);
    shimmerAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::shimmer.getParamID(), shimmerSlider);

    juce::StringArray pitchNames;
    for (const auto& pitch : Params::getShimmerPitches())
        pitchNames.add (pitch.name);

    setupComboBox (characterGroup, shimmerPitchBox, shimmerPitchLabel, "Shimmer Pitch",
                   "How far each pass through the reverb shifts, from an octave down to two octaves up.", pitchNames);
    shimmerPitchBox.setExplicitFocusOrder (5);
    shimmerPitchAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::shimmerPitch.getParamID(), shimmerPitchBox);

    // ---- Stereo width ----------------------------------------------------
    setupSlider (characterGroup, widthSlider, widthLabel, "Stereo Width",
                 "Delays the right channel of the echoes by 0 to 100 milliseconds to widen them. "
                 "Does not affect the dry signal. Stereo only.");
    widthSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
    widthSlider.setExplicitFocusOrder (6);
    widthAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::stereoWidthMs.getParamID(), widthSlider);

    // ---- Filters ---------------------------------------------------------
    setupFrequencySlider (filtersGroup, lowCutSlider, lowCutLabel, "Low Cut",
                          "High-pass filter on the echoes, off or 21 hertz to 2 kilohertz. Turn fully down for off.",
                          Params::lowCutMinHz);
    lowCutSlider.setExplicitFocusOrder (1);
    lowCutAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::lowCut.getParamID(), lowCutSlider);

    setupFrequencySlider (filtersGroup, highCutSlider, highCutLabel, "High Cut",
                          "Low-pass filter on the echoes, 500 hertz to 20 kilohertz or off. Turn fully up for off.",
                          Params::highCutMaxHz);
    highCutSlider.setExplicitFocusOrder (2);
    highCutAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::highCut.getParamID(), highCutSlider);

    setupComboBox (filtersGroup, filterPosBox, filterPosLabel, "Filter Position",
                   "In Feedback Loop filters every repeat, so echoes get progressively darker or thinner. "
                   "Output Only filters the echoes once, without changing the feedback.",
                   Params::getFilterPositionNames());
    filterPosBox.setExplicitFocusOrder (3);
    filterPosAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::filterPos.getParamID(), filterPosBox);

    // ---- Modulation ------------------------------------------------------
    setupComboBox (modulationGroup, modTypeBox, modTypeLabel, "Modulation",
                   "Chorus, flanger or phaser on the echoes, or off.", Params::getModulationTypeNames());
    modTypeBox.setExplicitFocusOrder (1);
    modTypeAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::modType.getParamID(), modTypeBox);

    setupComboBox (modulationGroup, modPosBox, modPosLabel, "Modulation Position",
                   "In Feedback Loop modulates every repeat, so echoes get thicker or swirlier each time. "
                   "Output Only modulates the echoes once.",
                   Params::getFilterPositionNames());
    modPosBox.setExplicitFocusOrder (2);
    modPosAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::modPos.getParamID(), modPosBox);

    setupSlider (modulationGroup, modRateSlider, modRateLabel, "Modulation Rate", "Speed of the modulation, 0.05 to 8 hertz.");
    modRateSlider.spokenTextFromValue = [] (double v) { return Params::formatHertz ((float) v, true); };
    modRateSlider.setExplicitFocusOrder (3);
    modRateAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::modRate.getParamID(), modRateSlider);

    setupPercentSlider (modulationGroup, modDepthSlider, modDepthLabel, "Modulation Depth", "How far the modulation sweeps, 0 to 100 percent.");
    modDepthSlider.setExplicitFocusOrder (4);
    modDepthAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::modDepth.getParamID(), modDepthSlider);

    // ---- Perform settings ------------------------------------------------
    setupComboBox (gestureGroup, inputModeBox, inputModeLabel, "Input",
                   "Always sends the input into the delay. Throw Only sends it in only while Throw is held or latched.",
                   Params::getInputModeNames());
    inputModeBox.setExplicitFocusOrder (1);
    inputModeAttachment = std::make_unique<ComboBoxAttachment> (p.apvts, Params::ID::inputMode.getParamID(), inputModeBox);

    setupSlider (gestureGroup, throwLevelSlider, throwLevelLabel, "Throw Level",
                 "Level of the input sent into the delay while Throw is on, minus 12 to plus 12 decibels.");
    throwLevelSlider.spokenTextFromValue = [] (double v) { return Params::formatDecibels ((float) v, true); };
    throwLevelSlider.setNumKeyboardSteps (24);
    throwLevelSlider.setExplicitFocusOrder (2);
    throwLevelAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::throwLevelDb.getParamID(), throwLevelSlider);

    setupSlider (gestureGroup, freezeFadeSlider, freezeFadeLabel, "Freeze Fade",
                 "How long Freeze takes to fade in and out, from 10 milliseconds to 2 seconds. "
                 "Long fades swell into the frozen sound.");
    freezeFadeSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
    freezeFadeSlider.setExplicitFocusOrder (3);
    freezeFadeAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::freezeFadeMs.getParamID(), freezeFadeSlider);

    setupSlider (gestureGroup, tapestopTimeSlider, tapestopTimeLabel, "Tapestop Time",
                 "How long Tapestop takes to slow the echoes to a stop, from 100 milliseconds to 2 seconds. "
                 "Spinning back up takes half as long.");
    tapestopTimeSlider.spokenTextFromValue = [] (double v) { return Params::formatMilliseconds ((float) v, true); };
    tapestopTimeSlider.setExplicitFocusOrder (4);
    tapestopTimeAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::tapestopTimeMs.getParamID(), tapestopTimeSlider);

    setupPercentSlider (gestureGroup, runawayDriveSlider, runawayDriveLabel, "Runaway Drive",
                        "How hard Runaway pushes, 0 to 100 percent: feedback from 110 to 160 percent into the saturator. "
                        "Higher builds faster and distorts harder.");
    runawayDriveSlider.setExplicitFocusOrder (5);
    runawayDriveAttachment = std::make_unique<SliderAttachment> (p.apvts, Params::ID::runawayDrive.getParamID(), runawayDriveSlider);

    resetButton.setTitle ("Reset");
    resetButton.setDescription ("Releases every gesture and latch, including Freeze, and undoes "
                                "feedback and delay time changes made with the arrow keys in Perform mode.");
    resetButton.setWantsKeyboardFocus (true);
    resetButton.setExplicitFocusOrder (6);
    resetButton.onClick = [this] { processorRef.resetPerformance(); };
    gestureGroup.addAndMakeVisible (resetButton);

    // Announced however the reset happened: button, Backspace, host or MIDI.
    p.onPerformanceReset = []
    {
        juce::AccessibilityHandler::postAnnouncement ("Reset", juce::AccessibilityHandler::AnnouncementPriority::high);
    };

    p.onGestureLatched = [] (const juce::String& name, bool isOn)
    {
        juce::AccessibilityHandler::postAnnouncement (name + (isOn ? " latched" : " released"),
                                                      juce::AccessibilityHandler::AnnouncementPriority::high);
    };

    p.apvts.state.addListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    setResizable (false, false);
    setSize (mainWidth, editorHeight);

    // Shows the Taps panel however Multi-Tap is changed: here, by automation, MIDI or a preset.
    multiTapWatcher = std::make_unique<juce::ParameterAttachment> (
        *p.apvts.getParameter (Params::ID::multiTap.getParamID()),
        [this] (float value) { setTapsVisible (value >= 0.5f); });
    multiTapWatcher->sendInitialUpdate();
}

void EchoFactoryEditor::focusGained (FocusChangeType)
{
    // Only hand off when focus enters the editor, never on activation or a timer:
    // this preserves host focus and JUCE's restoration of the last focused child.
    if (auto traverser = createKeyboardFocusTraverser())
        if (auto* first = traverser->getDefaultComponent (this))
            first->grabKeyboardFocus();
}

EchoFactoryEditor::~EchoFactoryEditor()
{
    processorRef.onPerformanceReset = nullptr;
    processorRef.onGestureLatched = nullptr;
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    processorRef.apvts.state.removeListener (this);
    cancelPendingUpdate();

    if (saveDialog != nullptr)
        saveDialog->exitModalState (0);
}

void EchoFactoryEditor::setupSlider (ControlGroup& group, AccessibleSlider& slider, juce::Label& label,
                                    const juce::String& name, const juce::String& description)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);
    label.setAccessible (false); // on-screen only; the slider's title is what gets spoken

    slider.setTitle (name);
    slider.setDescription (description);
    slider.setNumKeyboardSteps (100);
    group.addAndMakeVisible (slider);
}

void EchoFactoryEditor::setupPercentSlider (ControlGroup& group, AccessibleSlider& slider, juce::Label& label,
                                           const juce::String& name, const juce::String& description)
{
    setupSlider (group, slider, label, name, description);
    slider.spokenTextFromValue = [] (double v) { return juce::String (juce::roundToInt (v)) + " percent"; };
}

void EchoFactoryEditor::setupFrequencySlider (ControlGroup& group, AccessibleSlider& slider, juce::Label& label, const juce::String& name,
                                             const juce::String& description, float offHz)
{
    setupSlider (group, slider, label, name, description);
    slider.spokenTextFromValue = [offHz] (double v) { return Params::formatFrequency ((float) v, offHz, true); };
}

void EchoFactoryEditor::setupComboBox (ControlGroup& group, juce::ComboBox& box, juce::Label& label, const juce::String& name,
                                      const juce::String& description, const juce::StringArray& items)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&box, false);
    label.setAccessible (false);

    box.addItemList (items, 1);
    box.setTitle (name);
    box.setDescription (description);
    box.setWantsKeyboardFocus (true);
    group.addAndMakeVisible (box);
}

void EchoFactoryEditor::updateTimeControlForSync (bool synced)
{
    if (timeAttachment != nullptr && synced == showingSyncDivisions)
        return;

    showingSyncDivisions = synced;
    timeAttachment.reset();

    timeAttachment = std::make_unique<SliderAttachment> (processorRef.apvts,
                                                         (synced ? Params::ID::syncDivision : Params::ID::delayTimeMs).getParamID(),
                                                         timeSlider);
    TimeKnob::configure (timeSlider, synced);

    if (synced)
    {
        timeLabel.setText ("Delay Division", juce::dontSendNotification);
        timeSlider.setTitle ("Delay Division");
        timeSlider.setDescription ("Delay length as a note division of the host tempo, from 1/64 note to 2 bars.");
    }
    else
    {
        timeLabel.setText ("Delay Time", juce::dontSendNotification);
        timeSlider.setTitle ("Delay Time");
        timeSlider.setDescription ("Delay time from 1 millisecond to 5 seconds.");
    }
}

void EchoFactoryEditor::setTapsVisible (bool shouldShow)
{
    tapsVisible = shouldShow;

    // Don't leave keyboard focus stranded in a hidden panel.
    if (! shouldShow && tapsPanel.hasKeyboardFocus (true))
        multiTapButton.grabKeyboardFocus();

    tapsPanel.setVisible (shouldShow && saveDialog == nullptr);
    setSize (shouldShow ? mainWidth + tapsWidth : mainWidth, editorHeight);
    repaint();
}

void EchoFactoryEditor::showSaveDialog()
{
    if (saveDialog != nullptr)
        return;

    juce::Component::SafePointer<EchoFactoryEditor> safeThis (this);

    saveDialog = std::make_unique<SavePresetDialog> (processorRef.presetManager, [safeThis] (bool)
    {
        if (safeThis == nullptr)
            return;

        safeThis->saveDialog.reset();
        safeThis->setMainControlsVisible (true);
        safeThis->presetBar.getMenuButton().grabKeyboardFocus();
    });

    // Hide the controls behind the dialog so screen readers can't wander into them.
    setMainControlsVisible (false);
    saveDialog->showIn (*this);
}

void EchoFactoryEditor::setMainControlsVisible (bool shouldBeVisible)
{
    // The groups take their controls and labels with them.
    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &performPad, &delayGroup, &characterGroup,
                                                              &filtersGroup, &modulationGroup, &gestureGroup })
        c->setVisible (shouldBeVisible);

    tapsPanel.setVisible (shouldBeVisible && tapsVisible);
}

void EchoFactoryEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == PresetManager::presetNameProperty)
        triggerAsyncUpdate();
}

void EchoFactoryEditor::valueTreeRedirected (juce::ValueTree&)
{
    triggerAsyncUpdate();
}

void EchoFactoryEditor::handleAsyncUpdate()
{
    presetBar.updatePresetName();
}

void EchoFactoryEditor::globalFocusChanged (juce::Component* focused)
{
    tapsPanel.scrollToShow (focused);
    repaint();
}

void EchoFactoryEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    g.setColour (Colours::topBar);
    g.fillRect (getLocalBounds().removeFromTop (48));

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("ECHO FACTORY", getLocalBounds().removeFromTop (48).reduced (16, 0), juce::Justification::centredLeft);

    // Dividers between the rows.
    g.setColour (Colours::topBar);

    for (auto dividerY : { (float) characterGroup.getY() - 4.0f, (float) filtersGroup.getY() - 4.0f, (float) performPad.getY() - 6.0f })
        g.drawLine (16.0f, dividerY, (float) mainWidth - 16.0f, dividerY, 1.5f);

    // And between the main controls and the Taps panel.
    if (tapsVisible)
        g.drawLine ((float) mainWidth, 64.0f, (float) mainWidth, (float) getHeight() - 16.0f, 1.5f);
}

void EchoFactoryEditor::paintOverChildren (juce::Graphics& g)
{
    // Visible focus indicator for sighted keyboard users.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    if (focused == nullptr || focused == this || ! isParentOf (focused) || ! focused->isShowing())
        return;

    if (saveDialog != nullptr && saveDialog->isParentOf (focused))
        return; // the dialog's own widgets show their focus

    const auto area = getLocalArea (focused, focused->getLocalBounds()).toFloat().expanded (3.0f);
    g.setColour (Colours::focus);
    g.drawRoundedRectangle (area, 6.0f, 2.0f);
}

void EchoFactoryEditor::resized()
{
    auto area = getLocalBounds().withWidth (mainWidth);

    // The Taps panel, when shown, takes the space to the right, under the top bar.
    tapsPanel.setBounds (getLocalBounds().withTrimmedLeft (mainWidth).withTrimmedTop (48).reduced (16));

    auto top = area.removeFromTop (48).reduced (16, 6);
    top.removeFromLeft (170); // title text
    presetBar.setBounds (top);

    area.reduce (16, 16);

    // Each group spans its row including the label strip above its controls,
    // and lays its children out in its own coordinates.
    const auto knob = [] (juce::Rectangle<int>& row, int width) { return row.removeFromLeft (width).reduced (6, 0); };
    const auto box  = [] (juce::Rectangle<int> column) { return column.withSizeKeepingCentre (column.getWidth() - 12, 30); };

    // Row 1: delay controls, seven columns
    delayGroup.setBounds (area.removeFromTop (delayRowHeight).withTrimmedBottom (8));
    auto row1 = delayGroup.getLocalBounds().withTrimmedTop (24); // room for labels
    const auto columnWidth = row1.getWidth() / 7;

    timeSlider.setBounds (knob (row1, columnWidth));
    smoothingSlider.setBounds (knob (row1, columnWidth));
    modesGroup.setBounds (row1.removeFromLeft (columnWidth).withSizeKeepingCentre (columnWidth - 12, 152));
    auto toggles = modesGroup.getLocalBounds();
    syncButton.setBounds (toggles.removeFromTop (40));

    for (auto* toggle : { &pingPongButton, &freezeButton, &multiTapButton })
    {
        toggles.removeFromTop (2);
        toggle->setBounds (toggles.removeFromTop (34));
    }

    feedbackSlider.setBounds (knob (row1, columnWidth));
    mixSlider.setBounds (knob (row1, columnWidth));
    feelSlider.setBounds (knob (row1, columnWidth));
    grooveSlider.setBounds (row1.reduced (6, 0));

    // Row 2: character, six columns
    characterGroup.setBounds (area.removeFromTop (rowHeight).withTrimmedTop (8).withTrimmedBottom (8));
    auto row2 = characterGroup.getLocalBounds().withTrimmedTop (24);
    const auto row2Column = row2.getWidth() / 6;

    styleBox.setBounds (box (row2.removeFromLeft (row2Column)));
    wearSlider.setBounds (knob (row2, row2Column));
    diffusionSlider.setBounds (knob (row2, row2Column));
    shimmerSlider.setBounds (knob (row2, row2Column));
    shimmerPitchBox.setBounds (box (row2.removeFromLeft (row2Column)));
    widthSlider.setBounds (row2.reduced (6, 0));

    // Row 3: filters (three columns) and modulation (three columns)
    auto row3 = area.removeFromTop (rowHeight).withTrimmedTop (8).withTrimmedBottom (8);
    const auto row3Column = row3.getWidth() / 6;

    filtersGroup.setBounds (row3.removeFromLeft (row3Column * 3));
    auto filters = filtersGroup.getLocalBounds().withTrimmedTop (24);
    lowCutSlider.setBounds (knob (filters, row3Column));
    highCutSlider.setBounds (knob (filters, row3Column));
    filterPosBox.setBounds (box (filters));

    modulationGroup.setBounds (row3);
    auto modulation = modulationGroup.getLocalBounds().withTrimmedTop (24);
    auto boxes = modulation.removeFromLeft (row3Column).reduced (6, 0); // type over position, each with its label above
    modTypeBox.setBounds (boxes.removeFromTop (boxes.getHeight() / 2).withSizeKeepingCentre (boxes.getWidth(), 30));
    modPosBox.setBounds (boxes.withSizeKeepingCentre (boxes.getWidth(), 30));
    modRateSlider.setBounds (knob (modulation, row3Column));
    modDepthSlider.setBounds (modulation.reduced (6, 0));

    // Row 4: perform pad and its settings, in seven columns
    auto row4 = area.withTrimmedTop (12).withTrimmedBottom (8);
    const auto performColumn = row4.getWidth() / 7;

    performPad.setBounds (row4.removeFromLeft (performColumn * 2).reduced (6, 0));
    gestureGroup.setBounds (row4);
    auto settings = gestureGroup.getLocalBounds().withTrimmedTop (20); // labels
    throwLevelSlider.setBounds (knob (settings, performColumn));
    freezeFadeSlider.setBounds (knob (settings, performColumn));
    tapestopTimeSlider.setBounds (knob (settings, performColumn));
    runawayDriveSlider.setBounds (knob (settings, performColumn));
    auto lastColumn = settings.reduced (6, 0);
    inputModeBox.setBounds (lastColumn.removeFromTop (lastColumn.getHeight() / 2).withSizeKeepingCentre (lastColumn.getWidth(), 30));
    resetButton.setBounds (lastColumn.withSizeKeepingCentre (lastColumn.getWidth(), 34));
}
