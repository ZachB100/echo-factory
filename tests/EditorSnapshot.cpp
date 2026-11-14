/*
    Paints the editor to PNG files, without a host, a window or audio:

        cmake --build build --target EditorSnapshot
        ./build/EditorSnapshot_artefacts/Debug/EditorSnapshot main.png taps.png

    The first file is the editor as it opens; the second has Multi-Tap on,
    so the Taps panel is shown.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include <cstdio>

namespace
{
bool savePng (juce::Component& component, const char* path)
{
    const auto image = component.createComponentSnapshot (component.getLocalBounds(), false, 1.0f);
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (stream.failedToOpen())
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream);
}
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: EditorSnapshot <main.png> <taps.png>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;

    EchoFactoryProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr || ! savePng (*editor, argv[1]))
        return 1;

    const auto mainWidth = editor->getWidth(), mainHeight = editor->getHeight();

    processor.apvts.getParameter (Params::ID::multiTap.getParamID())->setValueNotifyingHost (1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    if (! savePng (*editor, argv[2]))
        return 1;

    std::printf ("%dx%d and %dx%d\n", mainWidth, mainHeight, editor->getWidth(), editor->getHeight());
    return 0;
}
