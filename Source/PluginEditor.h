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
    void updateTimeControlForSync (bool synced);
    void showSaveDialog();
    void setMainControlsVisible (bool);
    void setTapsVisible (bool);

    // The main controls keep their width; Multi-Tap adds the Taps panel to the right.
    static constexpr int mainWidth = 860, tapsWidth = 560, editorHeight = 710;
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
    ControlGroup delayGroup     { "Delay", "Delay time, feedback and mix" };
    ControlGroup modesGroup     { "Modes", "Sync, ping-pong, freeze and multi-tap switches" };
    ControlGroup characterGroup { "Character", "Wear, diffusion and stereo width" };
    ControlGroup filtersGroup   { "Filters", "Low cut, high cut and filter position" };
    ControlGroup gestureGroup   { "Gesture Settings", "Input mode, how each gesture behaves, and reset" };

    AccessibleSlider timeSlider, smoothingSlider, feedbackSlider, mixSlider;
    juce::Label timeLabel, smoothingLabel, feedbackLabel, mixLabel;
    juce::ToggleButton syncButton { "Sync to host tempo" };
    juce::ToggleButton pingPongButton { "Ping-Pong" };
    juce::ToggleButton freezeButton { "Freeze" };
    juce::ToggleButton multiTapButton { "Multi-Tap" };

    AccessibleSlider widthSlider, lowCutSlider, highCutSlider;
    juce::Label widthLabel, lowCutLabel, highCutLabel, filterPosLabel;
    juce::ComboBox filterPosBox;

    PerformPad performPad;
    TapsPanel tapsPanel;
    AccessibleSlider throwLevelSlider, freezeFadeSlider, tapestopTimeSlider, runawayDriveSlider, wearSlider, diffusionSlider;
    juce::Label throwLevelLabel, freezeFadeLabel, tapestopTimeLabel, runawayDriveLabel, wearLabel, diffusionLabel, inputModeLabel;
    juce::ComboBox inputModeBox;
    juce::TextButton resetButton { "Reset" };

    std::unique_ptr<SliderAttachment> timeAttachment, smoothingAttachment, feedbackAttachment, mixAttachment;
    std::unique_ptr<SliderAttachment> widthAttachment, lowCutAttachment, highCutAttachment;
    std::unique_ptr<ButtonAttachment> syncButtonAttachment, pingPongButtonAttachment, freezeButtonAttachment, multiTapButtonAttachment;
    std::unique_ptr<SliderAttachment> throwLevelAttachment, freezeFadeAttachment, tapestopTimeAttachment, runawayDriveAttachment, wearAttachment, diffusionAttachment;
    std::unique_ptr<ComboBoxAttachment> filterPosAttachment, inputModeAttachment;
    std::unique_ptr<juce::ParameterAttachment> syncWatcher, multiTapWatcher;
    bool showingSyncDivisions = false;

    std::unique_ptr<SavePresetDialog> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoFactoryEditor)
};
