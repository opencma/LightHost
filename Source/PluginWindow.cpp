#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.h"
#include "HostDesign.h"

static Array <PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow (Component* const pluginEditor,
                            AudioProcessorGraph::Node* const o,
                            WindowFormatType t)
    : DocumentWindow (pluginEditor->getName(), HostDesign::canvas,
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner (o),
      type (t),
      lastXProp (getLastXProp(t)), lastYProp (getLastYProp(t))
{
    setSize (400, 300);
    setUsingNativeTitleBar(true);
    HostDesign::updateNativeTitleBar(*this);
    setContentOwned (pluginEditor, true);

    setTopLeftPosition (owner->properties.getWithDefault (getLastXProp (type), Random::getSystemRandom().nextInt (500)),
                        owner->properties.getWithDefault (getLastYProp (type), Random::getSystemRandom().nextInt (500)));


    activePluginWindows.add (this);

}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    while (!activePluginWindows.isEmpty())
        delete activePluginWindows.getLast();
}

void PluginWindow::closeWindowsFor(AudioProcessorGraph::Node* node)
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == node)
            delete activePluginWindows.getUnchecked(i);
}

void PluginWindow::setTitleFor(AudioProcessorGraph::Node* node, const String& title)
{
    for (auto* window : activePluginWindows)
        if (window->owner == node)
            window->setName(title);
}

bool PluginWindow::containsActiveWindows()
{
    return activePluginWindows.size() > 0;
}

PluginWindow* PluginWindow::getWindowFor (AudioProcessorGraph::Node* const node,
                                          WindowFormatType type)
{
    jassert (node != nullptr);

    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == node)
            return activePluginWindows.getUnchecked(i);

    AudioProcessor* processor = node->getProcessor();
    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor->createEditorIfNeeded();

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic)
            ui = new GenericAudioProcessorEditor (processor);
    }

    if (ui != nullptr)
    {
        if (AudioPluginInstance* const plugin = dynamic_cast<AudioPluginInstance*> (processor))
            ui->setName (plugin->getName());

        return new PluginWindow (ui, node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue (this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set (lastXProp, getX());
    owner->properties.set (lastYProp, getY());
}

void PluginWindow::closeButtonPressed()
{
    delete this;
}

void PluginWindow::lookAndFeelChanged()
{
    DocumentWindow::lookAndFeelChanged();
    setBackgroundColour(HostDesign::canvas);
    HostDesign::updateNativeTitleBar(*this);
}
