#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

// Preserve JUCE's standalone audio/settings window, but explicitly initialise
// native and control focus. Merely showing the window leaves NVDA on the window
// root on Windows, where Tab cannot enter the controls.
class EchoFactoryStandaloneWindow final : public juce::StandaloneFilterWindow
{
public:
    using juce::StandaloneFilterWindow::StandaloneFilterWindow;

    void focusGained (FocusChangeType) override
    {
        focusEditor();
    }

    void focusEditor()
    {
        // JUCE's top-level window itself wants focus. Tab cannot traverse from
        // that root because it has no parent; enter the editor instead.
        if (auto* processor = getAudioProcessor())
            if (auto* editor = processor->getActiveEditor())
                editor->grabKeyboardFocus();
    }
};

class EchoFactoryStandaloneApp final : public juce::JUCEApplication
{
public:
    EchoFactoryStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = JucePlugin_Name;
        options.filenameSuffix = ".settings";
        properties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        window = std::make_unique<EchoFactoryStandaloneWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            properties.getUserSettings(), false);
        window->setVisible (true);
        window->toFront (true);
        window->focusEditor();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        else
            quit();
    }

    void shutdown() override
    {
        window = nullptr;
        properties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<EchoFactoryStandaloneWindow> window;
};

// The JUCE standalone wrapper supplies the platform entry point.
JUCE_CREATE_APPLICATION_DEFINE (EchoFactoryStandaloneApp)
