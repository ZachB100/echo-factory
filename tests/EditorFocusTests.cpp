#include "PluginEditor.h"
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    EchoFactoryProcessor processor;
    juce::Component wrapper;
    EchoFactoryEditor editor (processor);
    wrapper.addAndMakeVisible (editor);
    wrapper.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);

    int failures = 0;
    const auto check = [&] (bool ok, const char* message)
    {
        std::printf ("%s: %s\n", ok ? "PASS" : "FAIL", message);
        if (! ok) ++failures;
    };

    // The native plugin/standalone peer begins traversal at its outer wrapper.
    // Previously this returned nullptr: the editor stopped traversal but could
    // not receive focus, making all of its controls unreachable from outside.
    juce::KeyboardFocusTraverser traversal;
    check (traversal.getDefaultComponent (&wrapper) == &editor,
           "outer window can enter editor without a mouse click");
    auto* first = traversal.getDefaultComponent (&editor);
    check (first != nullptr && first->getTitle().startsWith ("Preset menu"),
           "editor focus handoff resolves to preset menu");

    const auto controls = traversal.getAllComponents (&editor);
    check (controls.size() >= 20, "main controls are keyboard reachable");
    for (size_t i = 1; i < controls.size(); ++i)
    {
        check (traversal.getNextComponent (controls[i - 1]) == controls[i],
               "forward traversal reaches next control");
        check (traversal.getPreviousComponent (controls[i]) == controls[i - 1],
               "backward traversal reaches previous control");
    }
    check (std::find (controls.begin(), controls.end(), &editor) == controls.end(),
           "editor container is not an extra stop inside its own tab order");
    if (first != nullptr)
        check (first->isAccessible(),
               "entry control is enabled for accessibility");

    std::printf ("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
