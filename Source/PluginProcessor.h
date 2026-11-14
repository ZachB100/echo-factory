#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DelayEngine.h"
#include "PresetManager.h"

class EchoFactoryProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::ValueTree::Listener,
                             private juce::Timer
{
public:
    EchoFactoryProcessor();
    ~EchoFactoryProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.1; } // two max-length ping-pong repeats + width

    // One host-visible program; presets are handled by PresetManager.
    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { apvts };

    /** Releases every gesture and latch, including Freeze. Message thread only. */
    void resetPerformance();

    /** Called on the message thread after any reset, including one from the host or MIDI. */
    std::function<void()> onPerformanceReset;

    /** Called on the message thread when a MIDI latch note toggles a gesture. */
    std::function<void (const juce::String& gestureName, bool isOn)> onGestureLatched;

    /** Perform-mode arrow keys. Message thread only; Reset undoes them. */
    void nudgeFeedback (float deltaPercent);
    void nudgeDelayTime (int direction);

    /** True while a MIDI note or the sustain pedal holds this gesture. */
    bool isGestureHeldByMidi (const juce::String& paramID) const;

private:
    // The Reset parameter can be set from any thread, so it only raises a flag
    // that the timer handles on the message thread.
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    std::atomic<bool> resetRequested { false };

    // ---- Taps (Taps.h) ---------------------------------------------------------
    // Any change under the TAPS tree only raises a flag (state can be restored on
    // any thread); the timer hands the taps to the engine on the message thread.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    std::atomic<bool> tapsDirty { true };

    // ---- MIDI (fixed map, see docs/perform-map.md) ---------------------------
    // The audio thread only records what arrived; held notes act immediately,
    // while latches and CCs change parameters from the timer on the message thread.
    static constexpr int numGestures = 5;
    void handleMidi (const juce::MidiBuffer&);
    bool isGestureOn (int index) const;

    std::array<std::atomic<float>*, numGestures> gestureParams {};
    std::array<std::atomic<bool>, numGestures> midiNoteHeld {};
    std::atomic<bool> sustainHeld { false };
    std::atomic<juce::uint32> latchRequests { 0 };
    std::array<std::atomic<float>, 128> pendingCC; // normalised value, or -1 for none

    // Values from before the first arrow-key nudge, restored by Reset.
    std::optional<float> feedbackBaseline, delayTimeBaseline, syncDivisionBaseline;

    float getTargetDelayMs() const;
    void updateEngineParameters();

    DelayEngine engine;

    std::atomic<float>* delayTimeParam    = nullptr;
    std::atomic<float>* smoothingParam    = nullptr;
    std::atomic<float>* syncParam         = nullptr;
    std::atomic<float>* syncDivisionParam = nullptr;
    std::atomic<float>* feedbackParam     = nullptr;
    std::atomic<float>* mixParam          = nullptr;
    std::atomic<float>* lowCutParam       = nullptr;
    std::atomic<float>* highCutParam      = nullptr;
    std::atomic<float>* filterPosParam    = nullptr;
    std::atomic<float>* pingPongParam     = nullptr;
    std::atomic<float>* widthParam        = nullptr;
    std::atomic<float>* inputModeParam    = nullptr;
    std::atomic<float>* throwLevelParam   = nullptr;
    std::atomic<float>* freezeFadeParam   = nullptr;
    std::atomic<float>* runawayDriveParam = nullptr;
    std::atomic<float>* tapestopTimeParam = nullptr;
    std::atomic<float>* wearParam         = nullptr;
    std::atomic<float>* diffusionParam    = nullptr;
    std::atomic<float>* multiTapParam     = nullptr;
    std::atomic<float>* styleParam        = nullptr;
    std::atomic<float>* shimmerParam      = nullptr;
    std::atomic<float>* shimmerPitchParam = nullptr;
    std::atomic<float>* modTypeParam      = nullptr;
    std::atomic<float>* modRateParam      = nullptr;
    std::atomic<float>* modDepthParam     = nullptr;
    std::atomic<float>* modPosParam       = nullptr;
    std::atomic<float>* feelParam         = nullptr;
    std::atomic<float>* grooveParam       = nullptr;

    double hostBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoFactoryProcessor)
};
