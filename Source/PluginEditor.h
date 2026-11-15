#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/AccessibleSlider.h"
#include "ui/ControlGroup.h"
#include "ui/PerformPad.h"
#include "ui/PresetBar.h"
#include "ui/SavePresetDialog.h"
#include "ui/TapsPanel.h"

class EchoFactoryEditor : public juce::AudioProcessorEditor,
                          private juce::ValueTree::Listener,
                          private juce::AsyncUpdater,
                          private juce::FocusChangeListener
{
public:
    explicit EchoFactoryEditor (EchoFactoryProcessor&);
    ~EchoFactoryEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void focusGained (FocusChangeType) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void setupSlider (ControlGroup&, AccessibleSlider&, juce::Label&, const juce::String& name, const juce::String& description);
    void setupPercentSlider (ControlGroup&, AccessibleSlider&, juce::Label&, const juce::String& name, const juce::String& description);
    void setupFrequencySlider (ControlGroup&, AccessibleSlider&, juce::Label&, const juce::String& name, const juce::String& description, float offHz);
    void setupComboBox (ControlGroup&, juce::ComboBox&, juce::Label&, const juce::String& name, const juce::String& description,
                        const juce::StringArray& items);
    void updateTimeControlForSync (bool synced);
    void showSaveDialog();
    void setMainControlsVisible (bool);
    void setTapsVisible (bool);

    // The main controls keep their width; Multi-Tap adds the Taps panel to the right.
    static constexpr int mainWidth = 980, tapsWidth = 560, editorHeight = 880;
    static constexpr int delayRowHeight = 196, rowHeight = 150;
    bool tapsVisible = false;

    // ValueTree::Listener — watches the preset name, including state restored by the host.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override;

    // FocusChangeListener — repaints the visible keyboard focus outline.
    void globalFocusChanged (juce::Component*) override;

    EchoFactoryProcessor& processorRef;

    PresetBar presetBar;

    // Screen-reader groups. Declared before the controls they hold, so they outlive them.
    ControlGroup delayGroup      { "Delay", "Delay time, feedback, mix, feel and groove" };
    ControlGroup modesGroup      { "Modes", "Sync, ping-pong, freeze and multi-tap switches" };
    ControlGroup characterGroup  { "Character", "Echo style, wear, diffusion, shimmer and stereo width" };
    ControlGroup filtersGroup    { "Filters", "Low cut, high cut and filter position" };
    ControlGroup modulationGroup { "Modulation", "Chorus, flanger or phaser on the echoes, with rate, depth and position" };
    ControlGroup brandonGroup    { "Brandon", "The chaos mode and its controls: feedback, orbit, crush, pitch jumps and chaos" };
    ControlGroup gestureGroup    { "Gesture Settings", "Input mode, how each gesture behaves, and reset" };

    AccessibleSlider timeSlider, smoothingSlider, feedbackSlider, mixSlider, feelSlider, grooveSlider;
    juce::Label timeLabel, smoothingLabel, feedbackLabel, mixLabel, feelLabel, grooveLabel;
    juce::ToggleButton syncButton { "Sync to host tempo" };
    juce::ToggleButton pingPongButton { "Ping-Pong" };
    juce::ToggleButton freezeButton { "Freeze" };
    juce::ToggleButton multiTapButton { "Multi-Tap" };

    juce::ComboBox styleBox, shimmerPitchBox;
    AccessibleSlider wearSlider, diffusionSlider, shimmerSlider, widthSlider;
    juce::Label styleLabel, wearLabel, diffusionLabel, shimmerLabel, shimmerPitchLabel, widthLabel;

    AccessibleSlider lowCutSlider, highCutSlider;
    juce::Label lowCutLabel, highCutLabel, filterPosLabel;
    juce::ComboBox filterPosBox;

    juce::ComboBox modTypeBox, modPosBox;
    AccessibleSlider modRateSlider, modDepthSlider;
    juce::Label modTypeLabel, modPosLabel, modRateLabel, modDepthLabel;

    juce::ToggleButton brandonButton { "Brandon" };
    AccessibleSlider brandonFeedbackSlider, brandonOrbitSlider, brandonCrushSlider, brandonPitchSlider, brandonChaosSlider;
    juce::Label brandonFeedbackLabel, brandonOrbitLabel, brandonCrushLabel, brandonPitchLabel, brandonChaosLabel;

    PerformPad performPad;
    TapsPanel tapsPanel;
    AccessibleSlider throwLevelSlider, freezeFadeSlider, tapestopTimeSlider, runawayDriveSlider;
    juce::Label throwLevelLabel, freezeFadeLabel, tapestopTimeLabel, runawayDriveLabel, inputModeLabel;
    juce::ComboBox inputModeBox;
    juce::TextButton resetButton { "Reset" };

    std::unique_ptr<SliderAttachment> timeAttachment, smoothingAttachment, feedbackAttachment, mixAttachment, feelAttachment, grooveAttachment;
    std::unique_ptr<SliderAttachment> wearAttachment, diffusionAttachment, shimmerAttachment, widthAttachment, lowCutAttachment, highCutAttachment;
    std::unique_ptr<SliderAttachment> modRateAttachment, modDepthAttachment;
    std::unique_ptr<SliderAttachment> brandonFeedbackAttachment, brandonOrbitAttachment, brandonCrushAttachment, brandonPitchAttachment, brandonChaosAttachment;
    std::unique_ptr<ButtonAttachment> brandonButtonAttachment;
    std::unique_ptr<ButtonAttachment> syncButtonAttachment, pingPongButtonAttachment, freezeButtonAttachment, multiTapButtonAttachment;
    std::unique_ptr<SliderAttachment> throwLevelAttachment, freezeFadeAttachment, tapestopTimeAttachment, runawayDriveAttachment;
    std::unique_ptr<ComboBoxAttachment> styleAttachment, shimmerPitchAttachment, filterPosAttachment, modTypeAttachment, modPosAttachment, inputModeAttachment;
    std::unique_ptr<juce::ParameterAttachment> syncWatcher, multiTapWatcher;
    bool showingSyncDivisions = false;

    std::unique_ptr<SavePresetDialog> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoFactoryEditor)
};
