//
//  IconMenu.cpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#include <JuceHeader.h>
#include <BinaryData.h>
#include "ErrorReporting.hpp"
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include "PluginChannelRouting.hpp"
#include "StereoRouting.hpp"
#include "UpdateManifestValidation.hpp"
#include "GlobalBypassShortcut.hpp"
#if JUCE_LINUX
#include "LinuxUpdatePackage.hpp"
#endif
#include <algorithm>
#include <utility>
#if ! JUCE_WINDOWS
#include <spawn.h>
#include <string>
#include <unistd.h>
extern char** environ;
#else
#include <Windows.h>
#endif

namespace
{
struct UpdateDownload
{
    MemoryBlock data;
    String error;
    int status = 0;
};

UpdateDownload readUpdateUrl (const String& url, size_t limit)
{
    UpdateDownload result;
    const auto options = URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs (15000)
                             .withStatusCode (&result.status);
    auto stream = URL (url).createInputStream (options);
    if (stream == nullptr || result.status != 200)
    {
        result.error = "The update server returned HTTP " + String (result.status) + ".";
        return result;
    }
    char buffer[65536];
    for (;;)
    {
        const auto count = stream->read (buffer, sizeof (buffer));
        if (count <= 0)
        {
            if (! stream->isExhausted())
                result.error = "The update download was interrupted.";
            break;
        }
        if (result.data.getSize() + (size_t) count > limit)
        {
            result.error = "The update response exceeds the permitted size.";
            break;
        }
        result.data.append (buffer, (size_t) count);
    }
    if (result.data.getSize() == 0 && result.error.isEmpty())
        result.error = "The update response was empty.";
    return result;
}

String currentUpdatePlatform()
{
#if JUCE_WINDOWS
    #if defined(_M_ARM64) || defined(__aarch64__)
    return "windows-arm64";
    #else
    return "windows-x64";
    #endif
#elif JUCE_MAC
    #if defined(__arm64__) || defined(__aarch64__)
    return "macos-arm64";
    #else
    return "macos-x64";
    #endif
#elif JUCE_LINUX
    #if defined(__aarch64__)
    return "linux-arm64-package";
    #else
    return "linux-x64-package";
    #endif
#else
    return {};
#endif
}

File updateStatusFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Light Host").getChildFile ("update-status.txt");
}

#if ! JUCE_WINDOWS
bool launchUpdateHelper (const File& helper, const std::vector<String>& arguments)
{
    std::vector<std::string> ownedArguments;
    ownedArguments.reserve (arguments.size() + 1);
    ownedArguments.push_back (helper.getFullPathName().toStdString());
    for (const auto& argument : arguments)
        ownedArguments.push_back (argument.toStdString());

    std::vector<char*> argv;
    argv.reserve (ownedArguments.size() + 1);
    for (auto& argument : ownedArguments)
        argv.push_back (argument.data());
    argv.push_back (nullptr);

    pid_t child = 0;
    return posix_spawn (&child, argv[0], nullptr, nullptr, argv.data(), environ) == 0;
}
#endif

}

namespace
{
std::vector<PluginDescription> findPluginDescriptionsForFile (const File& file)
{
    AudioPluginFormatManager formatManager;
    addDefaultFormatsToManager (formatManager);
    OwnedArray<PluginDescription> found;
    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (file.getFullPathName()))
            format->findAllTypesForFile (found, file.getFullPathName());

    std::vector<PluginDescription> candidates;
    candidates.reserve ((size_t) found.size());
    for (const auto* description : found)
        candidates.push_back (*description);
    return candidates;
}
}

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
	PluginListWindow(IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
		: DocumentWindow("Available Plugins", Colours::white,
			DocumentWindow::minimiseButton | DocumentWindow::closeButton),
		owner(owner_)
	{
		const File deadMansPedalFile(getAppProperties().getUserSettings()
			->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

		setContentOwned(new PluginListComponent(pluginFormatManager,
			owner.knownPluginList,
			deadMansPedalFile,
			getAppProperties().getUserSettings()), true);

		setUsingNativeTitleBar(true);
		setResizable(true, false);
		setResizeLimits(300, 400, 800, 1500);
		setTopLeftPosition(60, 60);

		restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
		setVisible(true);
	}

	~PluginListWindow() override
	{
		getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());

		clearContentComponent();
	}

	void closeButtonPressed() override
	{
        owner.removePluginsLackingInputOutput();
        #if JUCE_MAC
        Process::setDockIconVisible(false);
        #endif
		owner.pluginListWindow = nullptr;
	}

private:
	IconMenu& owner;
};

class IconMenu::MainWindow : public DocumentWindow, private ListBoxModel, private Timer
{
public:
    explicit MainWindow (IconMenu& menu)
        : DocumentWindow ("Light Host", Colours::darkgrey, DocumentWindow::allButtons),
          owner (menu), list ("Active plugins", this)
    {
        setUsingNativeTitleBar (true);
        auto* panel = new Component();
        panel->addAndMakeVisible (list);
        list.setBounds (10, 10, 480, 250);
        const char* labels[buttonCount] = { "Edit", "Locate...", "Bypass", "Move up", "Move down", "Remove",
                                            "Audio settings", "Scan plugins", "Add plugin", "Bypass shortcut...",
                                            "Plugin channels...", "Add plugin file..." };
        for (int i = 0; i < buttonCount; ++i)
        {
            buttons[i].setButtonText (labels[i]);
            buttons[i].setBounds (10 + (i % 3) * 160, 270 + (i / 3) * 38, 150, 30);
            panel->addAndMakeVisible (buttons[i]);
            buttons[i].onClick = [this, i] { perform (i); };
        }
        buttons[10].setTooltip ("Choose which independent input channels use the selected plugin.");
        routingLabel.setText ("Routing:", dontSendNotification);
        routingLabel.setBounds (10, 425, 90, 24);
        panel->addAndMakeVisible (routingLabel);
        routing.addItem ("Stereo pair", 1);
        routing.addItem ("Independent channels (up to 8)", 2);
        routing.setName ("Audio routing");
        routing.setSelectedId (owner.usesIndependentChannelRouting() ? 2 : 1, dontSendNotification);
        routing.setTooltip ("Send each channel individually through the chain - avoids downmixing to stereo and back up to surround. "
                            "Each channel gets its own plugin instances. Stereo-only plugins receive that one channel on both inputs.");
        routing.setBounds (100, 425, 390, 24);
        panel->addAndMakeVisible (routing);
        routing.onChange = [this]
        {
            owner.setIndependentChannelRouting (routing.getSelectedId() == 2);
            routing.setSelectedId (owner.usesIndependentChannelRouting() ? 2 : 1, dontSendNotification);
        };
        latency.setJustificationType (Justification::centredLeft);
        latency.setFont (FontOptions (13.0f));
        latency.setBounds (10, 455, 480, 24);
        latency.setTooltip ("This is the latency reported by plugins in the active graph. Audio-device and driver latency are additional.");
        panel->addAndMakeVisible (latency);
        updateLatency();
        panel->setSize (500, 485);
        setContentOwned (panel, true);
        centreWithSize (500, 485);
        setVisible (true);
        #if JUCE_LINUX
        startTimer (50);
        #else
        startTimer (500);
        #endif
        globalShortcut = std::make_unique<GlobalBypassShortcut> ([this]
        {
            if (! isPositiveAndBelow (list.getSelectedRow(), getNumRows()))
            {
                showErrorWithDiagnostics ("No plugin selected",
                                          "Select a plugin in the Light Host window before using the bypass shortcut.");
                return;
            }
            perform (2);
        });
        #if JUCE_WINDOWS
        const auto defaultShortcut = String();
        #else
        const auto defaultShortcut = String ("ctrl + b");
        #endif
        const auto shortcut = getAppProperties().getUserSettings()->getValue ("bypassShortcut", defaultShortcut);
        const auto error = globalShortcut->setShortcut (shortcut);
        globalShortcutAvailable = shortcut.trim().isNotEmpty() && error.isEmpty();
        if (error.isNotEmpty())
            showErrorWithDiagnostics ("Global bypass shortcut unavailable", error);
    }

    void closeButtonPressed() override { setVisible (false); }

private:
    static constexpr int buttonCount = 12;

    bool keyPressed (const KeyPress& key) override
    {
        if (! globalShortcutAvailable)
        {
            #if JUCE_WINDOWS
            const auto shortcutDefault = String();
            #else
            const auto shortcutDefault = String ("ctrl + b");
            #endif
            const auto description = getAppProperties().getUserSettings()->getValue ("bypassShortcut", shortcutDefault);
            if (key == KeyPress::createFromDescription (description))
            {
                perform (2);
                return true;
            }
        }
        return DocumentWindow::keyPressed (key);
    }

    int getNumRows() override { return (int) owner.getTimeSortedList().size(); }

    void paintListBoxItem (int row, Graphics& g, int width, int height, bool selected) override
    {
        const auto plugins = owner.getTimeSortedList();
        if (! isPositiveAndBelow (row, (int) plugins.size()))
            return;
        if (selected)
            g.fillAll (Colours::darkblue);
        g.setColour (Colours::white);
        String text = plugins[(size_t) row].name;
        if (row < owner.pluginLoadErrors.size() && owner.pluginLoadErrors[row].isNotEmpty())
            text += "  [not loaded]";
        if (getAppProperties().getUserSettings()->getBoolValue (getKey ("bypass", plugins[(size_t) row])))
            text += "  [bypassed]";
        if (owner.usesIndependentChannelRouting())
        {
            StringArray channels;
            for (int channel = 0; channel < 8; ++channel)
                if (pluginUsesChannel (plugins[(size_t) row].channelMask, channel))
                    channels.add (String (channel + 1));
            text += "  [ch " + (channels.isEmpty() ? String ("none") : channels.joinIntoString (",")) + "]";
        }
        g.drawText (text, 8, 0, width - 16, height, Justification::centredLeft);
    }

    void timerCallback() override
    {
        globalShortcut->poll();
        #if JUCE_LINUX
        const auto now = Time::getMillisecondCounter();
        if (now - lastRefreshTime < 500)
            return;
        lastRefreshTime = now;
        #endif
        list.updateContent();
        list.repaint();
        updateLatency();
    }

    void updateLatency()
    {
        const int samples = jmax (0, owner.graph.getLatencySamples());
        const double sampleRate = owner.graph.getSampleRate();
        String text = "Plugin-reported latency: " + String (samples) + " samples";
        if (sampleRate > 0.0)
            text += " (" + String (1000.0 * samples / sampleRate, 2) + " ms)";
        text += " - device/driver latency is additional.";
        latency.setText (text, dontSendNotification);
    }

    void perform (int action)
    {
        if (action == 9)
        {
            const String shortcutHelp = "Enter a combination such as ctrl + alt + b, or leave it empty to disable it. "
                                        "The global shortcut toggles the selected plugin even when another window is focused.";
            AlertWindow dialog ("Bypass shortcut",
                                shortcutHelp,
                                MessageBoxIconType::NoIcon);
            #if JUCE_WINDOWS
            const auto shortcutDefault = String();
            #else
            const auto shortcutDefault = String ("ctrl + b");
            #endif
            dialog.addTextEditor ("shortcut", getAppProperties().getUserSettings()->getValue (
                "bypassShortcut", shortcutDefault));
            dialog.addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            dialog.addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            if (dialog.runModalLoop() == 1)
            {
                const auto text = dialog.getTextEditorContents ("shortcut").trim();
                const auto error = globalShortcut->setShortcut (text);
                if (error.isNotEmpty())
                    showErrorWithDiagnostics ("Invalid or unavailable shortcut", error);
                else
                {
                    globalShortcutAvailable = text.isNotEmpty();
                    getAppProperties().getUserSettings()->setValue ("bypassShortcut", text);
                    if (! getAppProperties().saveIfNeeded())
                        showErrorWithDiagnostics ("Could not save shortcut", "The shortcut could not be saved.");
                }
            }
            return;
        }
        if (action == 6) { owner.showAudioSettings(); return; }
        if (action == 7) { owner.reloadPlugins(); return; }
        if (action == 8)
        {
            PopupMenu available;
            KnownPluginList::addToMenu (available, owner.knownPluginList.getTypes(), owner.pluginSortMethod);
            Component::SafePointer<MainWindow> safeThis (this);
            available.showMenuAsync (PopupMenu::Options().withTargetComponent (&buttons[8]),
                                     [safeThis] (int id) { if (safeThis != nullptr && id != 0) safeThis->invoke (id); });
            return;
        }
        if (action == 10)
        {
            const int row = list.getSelectedRow();
            const auto plugins = owner.getTimeSortedList();
            if (isPositiveAndBelow (row, (int) plugins.size()))
                owner.editPluginChannels (plugins[(size_t) row]);
            return;
        }
        if (action == 11)
        {
            owner.addPluginFile();
            return;
        }
        const int row = list.getSelectedRow();
        const int count = getNumRows();
        if (! isPositiveAndBelow (row, count))
            return;
        if ((action == 3 && row == 0) || (action == 4 && row == count - 1))
            return;
        const int ids[] = { owner.INDEX_EDIT, 6000000, owner.INDEX_BYPASS,
                            owner.INDEX_MOVE_UP, owner.INDEX_MOVE_DOWN, owner.INDEX_DELETE };
        invoke (ids[action] + row);
        list.updateContent();
        const int nextRow = action == 3 ? row - 1 : action == 4 ? row + 1 : row;
        list.selectRow (jmin (nextRow, getNumRows() - 1));
    }

    void invoke (int id)
    {
        owner.mainWindowAction = true;
        owner.menuIconLeftClicked = true;
        menuInvocationCallback (id, &owner);
        owner.mainWindowAction = false;
    }

    IconMenu& owner;
    ListBox list;
    TextButton buttons[buttonCount];
    Label latency;
    Label routingLabel;
    ComboBox routing;
    std::unique_ptr<GlobalBypassShortcut> globalShortcut;
    bool globalShortcutAvailable = false;
    uint32 lastRefreshTime = 0;
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000), INDEX_MOVE_UP(4000000), INDEX_MOVE_DOWN(5000000)
{
    // Initiialization
    addDefaultFormatsToManager(formatManager);
	#if JUCE_WINDOWS
	x = y = 0;
	#endif
    // Audio device
    std::unique_ptr<XmlElement> savedAudioState (getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    const String audioError = deviceManager.initialise(8, 8, savedAudioState.get(), false);
    if (audioError.isNotEmpty())
    {
        String details = "Could not open the selected audio device: " + audioError;
        if (savedAudioState != nullptr)
        {
            const String sameDevice = savedAudioState->getStringAttribute("audioDeviceName");
            const String input = sameDevice.isNotEmpty() ? sameDevice : savedAudioState->getStringAttribute("audioInputDeviceName");
            const String output = sameDevice.isNotEmpty() ? sameDevice : savedAudioState->getStringAttribute("audioOutputDeviceName");
            const double rate = savedAudioState->getDoubleAttribute("audioDeviceRate");
            details += "\nSaved device type: " + savedAudioState->getStringAttribute("deviceType", "(default)")
                     + "\nSaved input: " + (input.isNotEmpty() ? input : "(not specified)")
                     + "\nSaved output: " + (output.isNotEmpty() ? output : "(not specified)")
                     + "\nSaved sample rate: " + (rate > 0 ? String(rate, 0) + " Hz" : "automatic");
        }
        const String fallbackError = deviceManager.initialise(8, 8, nullptr, false);
        if (fallbackError.isEmpty())
        {
            std::unique_ptr<XmlElement> fallbackState (deviceManager.createStateXml());
            if (fallbackState != nullptr)
            {
                getAppProperties().getUserSettings()->setValue ("audioDeviceState", fallbackState.get());
                if (! getAppProperties().saveIfNeeded())
                    details += "\nThe default device was opened, but its settings could not be saved.";
                else
                    details += "\nA default device was opened and saved for the next launch.";
            }
            else
                details += "\nA default device was opened, but its state could not be read.";
        }
        else
            details += "\nCould not open a default device: " + fallbackError;
        showErrorWithDiagnostics("Audio device error", details + audioDeviceHelp());
    }
    lastAudioDeviceFingerprint = getAudioDeviceFingerprint();
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    deviceManager.addChangeListener (this);
    // Plugins - all
    std::unique_ptr<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    // Plugins - active
    restoreActivePlugins();
    loadActivePlugins();
	setIcon();
	setIconTooltip(JUCEApplication::getInstance()->getApplicationName());
    showMainWindow();
    const auto previousUpdateStatus = updateStatusFile();
    if (previousUpdateStatus.existsAsFile())
    {
        auto message = previousUpdateStatus.loadFileAsString().trim();
        if (! previousUpdateStatus.deleteFile())
            message << "\nCould not remove the update status file: " << previousUpdateStatus.getFullPathName();
        if (message.isNotEmpty())
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Light Host update", message);
    }
    checkForUpdates (false);
};

IconMenu::~IconMenu()
{
	savePluginStates();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
}

void IconMenu::setIcon()
{
	// Set menu icon
	#if JUCE_MAC
        const auto icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
        setIconImage(icon, icon);
	#else
		String defaultColor;
	#if JUCE_WINDOWS
		defaultColor = "white";
	#elif JUCE_LINUX
		defaultColor = "black";
	#endif
		if (!getAppProperties().getUserSettings()->containsKey("icon"))
			getAppProperties().getUserSettings()->setValue("icon", defaultColor);
		String color = getAppProperties().getUserSettings()->getValue("icon");
		Image icon;
		if (color.equalsIgnoreCase("white"))
			icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
		else if (color.equalsIgnoreCase("black"))
			icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
		setIconImage(icon, icon);
	#endif
}

void IconMenu::loadActivePlugins()
{
	typedef AudioProcessorGraph::NodeID NodeID;
	const NodeID INPUT(1000000);
	const NodeID OUTPUT(1000001);
	PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();
    pluginLoadErrors.clear();
    const auto* device = deviceManager.getCurrentAudioDevice();
    const int availableInputs = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 2;
    const int availableOutputs = device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 2;
    const double sampleRate = graph.getSampleRate() > 0 ? graph.getSampleRate() : 44100.0;
    const int blockSize = graph.getBlockSize() > 0 ? graph.getBlockSize() : 512;
    graph.setPlayConfigDetails (availableInputs, availableOutputs, sampleRate, blockSize);
    graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    StringArray connectionErrors;
    auto connect = [this, &connectionErrors](NodeID from, NodeID to)
    {
        connectionErrors.addArray (connectStereo (graph, from, to));
    };
    const auto sortedPlugins = getTimeSortedList();
    const bool independentChannels = usesIndependentChannelRouting();
    const int channelCount = independentChannels ? jmin (8, availableInputs, availableOutputs) : 1;
    if (independentChannels && channelCount == 0)
        showErrorWithDiagnostics ("No independent audio channels available",
                                  "Choose an audio device with at least one active input and output channel.");
    if (independentChannels && availableInputs != availableOutputs)
        showErrorWithDiagnostics ("Independent channel routing",
                                  "Input and output channel counts differ (" + String (availableInputs) + " in, "
                                  + String (availableOutputs) + " out). Only matching channels can be processed. "
                                  "Select matching channel counts in Audio Settings.");
	std::vector<NodeID> lastIds ((size_t) jmax (1, channelCount), INPUT);
    std::vector<int> lastOutputChannels ((size_t) jmax (1, channelCount), 0);
    std::vector<bool> chainHasPlugin ((size_t) jmax (1, channelCount), false);
	StringArray failures;
    for (int i = 0; i < (int) sortedPlugins.size(); ++i)
    {
        const auto& plugin = sortedPlugins[(size_t) i];
        const int instancesToCreate = independentChannels ? channelCount : 1;
        std::vector<std::unique_ptr<AudioPluginInstance>> instances;
        std::vector<int> instanceChannels;
        instances.reserve ((size_t) instancesToCreate);
        instanceChannels.reserve ((size_t) instancesToCreate);
        String pluginError;
        for (int channel = 0; channel < instancesToCreate; ++channel)
        {
            if (independentChannels && ! pluginUsesChannel (plugin.channelMask, channel))
                continue;
            String errorMessage;
            auto instance = formatManager.createPluginInstance (plugin, sampleRate, blockSize, errorMessage);
            if (instance == nullptr)
            {
                pluginError = errorMessage.isNotEmpty() ? errorMessage : "The plugin could not be loaded.";
                break;
            }
            const auto stateKey = getInstanceStateKey (plugin, channel, independentChannels);
            auto state = getAppProperties().getUserSettings()->getValue (stateKey);
            if (state.isEmpty() && independentChannels)
                state = getAppProperties().getUserSettings()->getValue (getKey ("state", plugin));
            MemoryBlock binary;
            binary.fromBase64Encoding (state);
            if (binary.getSize() > 0)
                instance->setStateInformation (binary.getData(), (int) binary.getSize());
            if (independentChannels)
            {
                auto layout = instance->getBusesLayout();
                if (layout.inputBuses.size() == 1 && layout.outputBuses.size() == 1
                    && layout.getMainInputChannels() > 0 && layout.getMainOutputChannels() > 0)
                {
                    layout.getChannelSet (true, 0) = AudioChannelSet::mono();
                    layout.getChannelSet (false, 0) = AudioChannelSet::mono();
                    if (instance->checkBusesLayoutSupported (layout))
                        instance->setBusesLayout (layout);
                }
            }
            instances.push_back (std::move (instance));
            instanceChannels.push_back (channel);
        }
        if (pluginError.isNotEmpty())
        {
            String details = plugin.name + " (" + plugin.pluginFormatName + ")";
            if (File::isAbsolutePath (plugin.fileOrIdentifier) && ! File (plugin.fileOrIdentifier).exists())
                details += "\nThe plugin file is missing: " + plugin.fileOrIdentifier
                         + "\nUse Locate plugin in its Active Plugins menu if it was moved.";
            details += "\n" + pluginError;
            pluginLoadErrors.add (details);
            failures.add (details);
            continue;
        }
        pluginLoadErrors.add (String());
        std::vector<NodeID> nodeIds ((size_t) instancesToCreate, NodeID (0));
        for (int instanceIndex = 0; instanceIndex < (int) instances.size(); ++instanceIndex)
        {
            const int channel = instanceChannels[(size_t) instanceIndex];
            const auto nodeId = getPluginNodeId (i, channel, independentChannels);
            if (graph.addNode (std::move (instances[(size_t) instanceIndex]), nodeId) == nullptr)
                pluginError = "Could not add a plugin instance to the audio graph.";
            else
                nodeIds[(size_t) channel] = nodeId;
        }
        if (pluginError.isNotEmpty())
        {
            pluginLoadErrors.set (pluginLoadErrors.size() - 1, pluginError);
            failures.add (plugin.name + "\n" + pluginError);
            continue;
        }
        const bool bypass = getAppProperties().getUserSettings()->getBoolValue (getKey ("bypass", plugin), false);
        if (bypass)
            continue;
        if (independentChannels)
        {
            for (int channel = 0; channel < channelCount; ++channel)
            {
                if (nodeIds[(size_t) channel] == NodeID (0))
                    continue;
                const auto source = lastIds[(size_t) channel];
                const int sourceChannel = chainHasPlugin[(size_t) channel] ? lastOutputChannels[(size_t) channel] : channel;
                connectionErrors.addArray (connectIndependentChannelInput (graph, source,
                                                                            nodeIds[(size_t) channel], sourceChannel));
                lastIds[(size_t) channel] = nodeIds[(size_t) channel];
                lastOutputChannels[(size_t) channel] = 0;
                chainHasPlugin[(size_t) channel] = true;
            }
        }
        else
        {
            const auto nodeId = nodeIds.front();
            if (! chainHasPlugin.front())
                connect (INPUT, nodeId);
            else
                connect (lastIds.front(), nodeId);
            lastIds.front() = nodeId;
            chainHasPlugin.front() = true;
        }
    }
    if (independentChannels)
    {
        for (int channel = 0; channel < channelCount; ++channel)
        {
            if (chainHasPlugin[(size_t) channel])
                connectionErrors.addArray (connectIndependentChannelOutput (graph, lastIds[(size_t) channel],
                                                                              OUTPUT, 0, channel));
            else
                connectionErrors.addArray (connectIndependentChannelOutput (graph, INPUT, OUTPUT,
                                                                              channel, channel));
        }
    }
    else if (chainHasPlugin.front())
        connect (lastIds.front(), OUTPUT);
    else
        connect (INPUT, OUTPUT);
    if (! connectionErrors.isEmpty())
        showErrorWithDiagnostics ("Audio routing error", connectionErrors.joinIntoString ("\n")
                                  + "\nCheck the selected audio device and plugin channel layouts.");
	if (!failures.isEmpty())
        showErrorWithDiagnostics("Some plugins could not be loaded",
                                 failures.joinIntoString("\n\n")
                                 + "\n\nTheir saved settings have been kept.");
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (changed == &deviceManager)
    {
        if (audioSettingsDialogOpen)
            return;
        auto* currentDevice = deviceManager.getCurrentAudioDevice();
        if (currentDevice == nullptr || ! currentDevice->isPlaying())
        {
            recoverAudioDevice();
            return;
        }
        const auto fingerprint = getAudioDeviceFingerprint();
        if (fingerprint == lastAudioDeviceFingerprint)
            return;
        lastAudioDeviceFingerprint = fingerprint;
        std::unique_ptr<XmlElement> audioState (deviceManager.createStateXml());
        if (audioState != nullptr)
        {
            auto* settings = getAppProperties().getUserSettings();
            settings->setValue ("audioDeviceState", audioState.get());
            if (! settings->saveIfNeeded())
                showErrorWithDiagnostics ("Could not save audio device changes",
                                          "The selected device, sample rate, or buffer size could not be saved.");
        }
        else
            showErrorWithDiagnostics ("Could not read audio device state",
                                      "The audio device manager returned no settings.");
        if (deviceManager.getCurrentAudioDevice() != nullptr)
        {
            if (! savePluginStates())
                return;
            loadActivePlugins();
        }
    }
    else if (changed == &knownPluginList)
    {
        std::unique_ptr<XmlElement> savedPluginList (knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginList", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
}

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    if (menuIconLeftClicked) {
        menu.addItem(4, "Open Light Host window");
        menu.addItem(5, "Check for updates");
        menu.addItem(1, "Preferences");
        menu.addItem(2, "Edit Plugins");
        menu.addSeparator();
		menu.addSectionHeader("Active Plugins");
        // Active plugins
        const auto timeSorted = getTimeSortedList();
        for (int i = 0; i < (int) timeSorted.size(); i++)
        {
            PopupMenu options;
            options.addItem(INDEX_EDIT + i, "Edit");
            if (i < pluginLoadErrors.size() && pluginLoadErrors[i].isNotEmpty())
                options.addItem(6000000 + i, "Locate plugin...");
			String key = getKey("bypass", timeSorted[i]);
			bool bypass = getAppProperties().getUserSettings()->getBoolValue(key);
			options.addItem(INDEX_BYPASS + i, "Bypass", true, bypass);
			options.addSeparator();
			options.addItem(INDEX_MOVE_UP + i, "Move Up", i > 0);
			options.addItem(INDEX_MOVE_DOWN + i, "Move Down", i < timeSorted.size() - 1);
			options.addSeparator();
            options.addItem(INDEX_DELETE + i, "Delete");
            menu.addSubMenu(timeSorted[(size_t) i].name, options);
        }
        menu.addSeparator();
		menu.addSectionHeader("Avaliable Plugins");
        // All plugins
        KnownPluginList::addToMenu(menu, knownPluginList.getTypes(), pluginSortMethod);
    }
    else
    {
        menu.addItem(1, "Quit");
		menu.addSeparator();
		menu.addItem(2, "Delete Plugin States");
		#if !JUCE_MAC
			menu.addItem(3, "Invert Icon Color");
		#endif
    }
	#if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), [this](int result) { menuInvocationCallback(result, this); });
	#else
	if (x == 0 || y == 0)
	{
		POINT iconLocation;
		iconLocation.x = 0;
		iconLocation.y = 0;
		GetCursorPos(&iconLocation);
		x = iconLocation.x;
		y = iconLocation.y;
	}
	juce::Rectangle<int> rect(x, y, 1, 1);
	menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect), [this](int result) { menuInvocationCallback(result, this); });
	#endif
}

void IconMenu::mouseDown(const MouseEvent& e)
{
	#if JUCE_MAC
		Process::setDockIconVisible(true);
	#endif
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    startTimer(50);
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    // Right click
    if ((!im->menuIconLeftClicked))
    {
		if (id == 1)
		{
			im->savePluginStates();
			return JUCEApplication::getInstance()->quit();
		}
		if (id == 2)
		{
			im->deletePluginStates();
			return im->loadActivePlugins();
		}
		if (id == 3)
		{
			String color = getAppProperties().getUserSettings()->getValue("icon");
			getAppProperties().getUserSettings()->setValue("icon", color.equalsIgnoreCase("black") ? "white" : "black");
			return im->setIcon();
		}
    }
	#if JUCE_MAC
    // Click elsewhere
    if (id == 0 && !PluginWindow::containsActiveWindows())
        Process::setDockIconVisible(false);
	#endif
    // Audio settings
    if (id == 1)
        im->showAudioSettings();
    // Reload
    if (id == 2)
        im->reloadPlugins();
    if (id == 4)
    {
        im->showMainWindow();
        return;
    }
    if (id == 5)
    {
        im->checkForUpdates (true);
        return;
    }
    const bool pluginMenuCommand = (id >= im->INDEX_EDIT && id < 7000000)
                                || KnownPluginList::getIndexChosenByMenu (im->knownPluginList.getTypes(), id) > -1;
    if (pluginMenuCommand && ! im->activeChainValid)
    {
        showErrorWithDiagnostics ("Plugin chain unavailable",
                                  "The saved instance list is invalid. No changes were made; restore a settings backup.");
        return;
    }
    // Plugins
    if (id > 2)
    {
        // Delete plugin
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
			int index = id - im->INDEX_DELETE;
			auto timeSorted = im->getTimeSortedList();
            if (! isPositiveAndBelow (index, (int) timeSorted.size()))
                return;
			if (! im->savePluginStates())
			    return;
            timeSorted.erase (timeSorted.begin() + index);
            if (! im->persistActivePlugins (timeSorted))
                return;
            auto* settings = getAppProperties().getUserSettings();
            settings->removeValue (getKey ("bypass", im->activePlugins[(size_t) index]));
            const String stateKey = getKey ("state", im->activePlugins[(size_t) index]);
            settings->removeValue (stateKey);
            for (int channel = 1; channel <= 8; ++channel)
                settings->removeValue (stateKey + "-channel-" + String (channel));
            if (! settings->saveIfNeeded())
                showErrorWithDiagnostics ("Could not delete plugin state",
                                          "The plugin was removed, but its saved state could not be deleted.");
            im->activePlugins = std::move (timeSorted);
			im->loadActivePlugins();
        }
        // Add plugin
        else if (KnownPluginList::getIndexChosenByMenu(im->knownPluginList.getTypes(), id) > -1)
        {
			PluginDescription plugin = im->knownPluginList.getTypes()[KnownPluginList::getIndexChosenByMenu(im->knownPluginList.getTypes(), id)];
			if (! im->savePluginStates())
			    return;
            ActivePlugin instance;
            static_cast<PluginDescription&> (instance) = plugin;
            instance.instanceId = Uuid().toString();
			auto updatedPlugins = im->activePlugins;
			updatedPlugins.push_back (std::move (instance));
			if (! im->persistActivePlugins (updatedPlugins))
			    return;
			im->activePlugins = std::move (updatedPlugins);
			im->loadActivePlugins();
        }
		// Bypass plugin
		else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
		{
			int index = id - im->INDEX_BYPASS;
			auto timeSorted = im->getTimeSortedList();
            if (! isPositiveAndBelow (index, (int) timeSorted.size()))
                return;
			if (! im->savePluginStates())
			    return;
			String key = getKey("bypass", timeSorted[index]);
			auto* settings = getAppProperties().getUserSettings();
			const bool bypassed = settings->getBoolValue (key);
			settings->setValue (key, ! bypassed);
			if (! settings->saveIfNeeded())
			{
			    settings->setValue (key, bypassed);
			    showErrorWithDiagnostics ("Could not save plugin bypass", "The bypass setting could not be saved.");
			    return;
			}
			im->loadActivePlugins();
		}
        // Show active plugin GUI
		else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
		    const int index = id - im->INDEX_EDIT;
            AudioProcessorGraph::Node::Ptr node;
            const int channels = im->usesIndependentChannelRouting() ? 8 : 1;
            for (int channel = 0; channel < channels && node == nullptr; ++channel)
                node = im->graph.getNodeForId (getPluginNodeId (index, channel,
                                                                im->usesIndependentChannelRouting()));
		    if (node != nullptr)
		    {
		        if (PluginWindow* const w = PluginWindow::getWindowFor (node, PluginWindow::Normal))
		            w->toFront (true);
		        else
		            showErrorWithDiagnostics ("Plugin editor unavailable",
		                                     "The plugin loaded, but its editor could not be opened.");
		    }
		    if (index < im->pluginLoadErrors.size() && im->pluginLoadErrors[index].isNotEmpty())
		        showErrorWithDiagnostics("Plugin could not be loaded",
		                                 im->pluginLoadErrors[index]
		                                 + "\n\nIts saved settings have been kept.");
		}
        else if (id >= 6000000 && id < 7000000)
        {
            const int index = id - 6000000;
            const auto plugins = im->getTimeSortedList();
            if (index >= 0 && index < (int) plugins.size())
                im->locatePlugin(plugins[(size_t) index]);
            return;
        }
		// Move plugin up the list
		else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_UP + 1000000)
		{
			if (! im->savePluginStates())
			    return;
			auto timeSorted = im->getTimeSortedList();
            const int index = id - im->INDEX_MOVE_UP;
            if (index <= 0 || index >= (int) timeSorted.size())
                return;
            std::swap (timeSorted[(size_t) index], timeSorted[(size_t) index - 1]);
            if (! im->persistActivePlugins (timeSorted))
                return;
            im->activePlugins = std::move (timeSorted);
			im->loadActivePlugins();
		}
		// Move plugin down the list
		else if (id >= im->INDEX_MOVE_DOWN && id < im->INDEX_MOVE_DOWN + 1000000)
		{
			if (! im->savePluginStates())
			    return;
			auto timeSorted = im->getTimeSortedList();
            const int index = id - im->INDEX_MOVE_DOWN;
            if (index < 0 || index + 1 >= (int) timeSorted.size())
                return;
            std::swap (timeSorted[(size_t) index], timeSorted[(size_t) index + 1]);
            if (! im->persistActivePlugins (timeSorted))
                return;
            im->activePlugins = std::move (timeSorted);
			im->loadActivePlugins();
		}
        // Update menu
        if (! im->mainWindowAction)
            im->startTimer(50);
    }
}

std::vector<IconMenu::ActivePlugin> IconMenu::getTimeSortedList()
{
    return activePlugins;
}

String IconMenu::getKey(String type, PluginDescription plugin)
{
	String key = "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
	return key;
}

String IconMenu::getKey (String type, const ActivePlugin& plugin)
{
    return "plugin-instance-" + type.toLowerCase() + "-" + plugin.instanceId;
}

String IconMenu::getInstanceStateKey (const ActivePlugin& plugin, int channel, bool independentChannels)
{
    const auto key = getKey ("state", plugin);
    return independentChannels ? key + "-channel-" + String (channel + 1) : key;
}

AudioProcessorGraph::NodeID IconMenu::getPluginNodeId (int pluginIndex, int channel, bool independentChannels)
{
    return AudioProcessorGraph::NodeID ((uint32) (1 + pluginIndex * (independentChannels ? 8 : 1)
                                                 + (independentChannels ? channel : 0)));
}

bool IconMenu::usesIndependentChannelRouting() const
{
    return getAppProperties().getUserSettings()->getBoolValue ("independentChannelRouting", false);
}

String IconMenu::getAudioDeviceFingerprint() const
{
    const auto& setup = deviceManager.getAudioDeviceSetup();
    return deviceManager.getCurrentAudioDeviceType() + "|" + setup.inputDeviceName + "|"
         + setup.outputDeviceName + "|" + setup.inputChannels.toString (16) + "|"
         + setup.outputChannels.toString (16) + "|" + String (setup.sampleRate, 3) + "|"
         + String (setup.bufferSize) + "|" + String ((int) setup.useDefaultInputChannels) + "|"
         + String ((int) setup.useDefaultOutputChannels);
}

void IconMenu::recoverAudioDevice()
{
    if (recoveringAudioDevice || audioSettingsDialogOpen)
        return;

    const auto now = Time::currentTimeMillis();
    if (now - lastAudioRecoveryAttempt < 10000)
        return;
    lastAudioRecoveryAttempt = now;
    recoveringAudioDevice = true;

    if (! savePluginStates())
    {
        recoveringAudioDevice = false;
        return;
    }

    auto* settings = getAppProperties().getUserSettings();
    std::unique_ptr<XmlElement> savedAudioState (settings->getXmlValue ("audioDeviceState"));
    String recoveryError = deviceManager.initialise (8, 8, savedAudioState.get(), false);
    auto* currentDevice = deviceManager.getCurrentAudioDevice();
    if (recoveryError.isNotEmpty() || currentDevice == nullptr || ! currentDevice->isPlaying())
    {
        const String selectedDeviceError = recoveryError.isNotEmpty()
                                               ? recoveryError
                                               : "The selected audio device did not resume playback.";
        const String fallbackError = deviceManager.initialise (8, 8, nullptr, false);
        currentDevice = deviceManager.getCurrentAudioDevice();
        if (fallbackError.isNotEmpty() || currentDevice == nullptr || ! currentDevice->isPlaying())
        {
            recoveringAudioDevice = false;
            showErrorWithDiagnostics ("Audio device recovery failed",
                                      "Could not restart the selected device: " + selectedDeviceError
                                      + "\nCould not open a default device: "
                                      + (fallbackError.isNotEmpty() ? fallbackError : "The device did not start.")
                                      + audioDeviceHelp());
            return;
        }

        std::unique_ptr<XmlElement> fallbackState (deviceManager.createStateXml());
        if (fallbackState == nullptr)
        {
            recoveringAudioDevice = false;
            showErrorWithDiagnostics ("Could not save audio fallback",
                                      "The default device started, but its settings could not be read.");
            return;
        }
        settings->setValue ("audioDeviceState", fallbackState.get());
        if (! settings->saveIfNeeded())
        {
            recoveringAudioDevice = false;
            showErrorWithDiagnostics ("Could not save audio fallback",
                                      "The default device started, but its settings could not be saved.");
            return;
        }
        lastAudioDeviceFingerprint = getAudioDeviceFingerprint();
        recoveringAudioDevice = false;
        loadActivePlugins();
        showErrorWithDiagnostics ("Audio device changed",
                                  "Light Host could not resume the selected audio device and switched to the system default.\n"
                                  + selectedDeviceError);
        return;
    }

    std::unique_ptr<XmlElement> recoveredState (deviceManager.createStateXml());
    if (recoveredState == nullptr)
    {
        recoveringAudioDevice = false;
        showErrorWithDiagnostics ("Could not save recovered audio device",
                                  "The audio device resumed, but its settings could not be read.");
        return;
    }
    settings->setValue ("audioDeviceState", recoveredState.get());
    if (! settings->saveIfNeeded())
    {
        recoveringAudioDevice = false;
        showErrorWithDiagnostics ("Could not save recovered audio device",
                                  "The audio device resumed, but its settings could not be saved.");
        return;
    }

    lastAudioDeviceFingerprint = getAudioDeviceFingerprint();
    recoveringAudioDevice = false;
    loadActivePlugins();
}

void IconMenu::setIndependentChannelRouting (bool enabled)
{
    if (! activeChainValid)
        return;
    if (usesIndependentChannelRouting() == enabled)
        return;
    if (! savePluginStates())
        return;
    auto* settings = getAppProperties().getUserSettings();
    if (! enabled)
        for (const auto& plugin : activePlugins)
        {
            const auto firstChannel = settings->getValue (getInstanceStateKey (plugin, 0, true));
            if (firstChannel.isNotEmpty())
                settings->setValue (getKey ("state", plugin), firstChannel);
        }
    settings->setValue ("independentChannelRouting", enabled);
    if (! getAppProperties().saveIfNeeded())
    {
        settings->setValue ("independentChannelRouting", ! enabled);
        showErrorWithDiagnostics ("Could not save audio routing", "The routing selection could not be saved.");
        return;
    }
    lastAudioDeviceFingerprint = getAudioDeviceFingerprint();
    loadActivePlugins();
}

void IconMenu::editPluginChannels (ActivePlugin plugin)
{
    if (! usesIndependentChannelRouting())
    {
        showErrorWithDiagnostics ("Independent channel routing is off",
                                  "Choose Independent channels in the main window before assigning plugins to channels.");
        return;
    }

    StringArray selectedChannels;
    for (int channel = 0; channel < 8; ++channel)
        if ((plugin.channelMask & (1u << channel)) != 0)
            selectedChannels.add (String (channel + 1));

    AlertWindow dialog ("Plugin channels",
                        "Enter the input channel numbers that should use this plugin, separated by commas (1-8). "
                        "Leave empty to bypass this plugin on every channel.",
                        MessageBoxIconType::NoIcon);
    dialog.addTextEditor ("channels", selectedChannels.joinIntoString (","));
    dialog.addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    dialog.addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    if (dialog.runModalLoop() != 1)
        return;

    uint32 channelMask = 0;
    StringArray requested;
    requested.addTokens (dialog.getTextEditorContents ("channels"), ",; ", "");
    requested.trim();
    for (const auto& channelText : requested)
    {
        if (channelText.isEmpty() || ! channelText.containsOnly ("0123456789"))
        {
            showErrorWithDiagnostics ("Invalid channel assignment",
                                      "Enter channel numbers from 1 to 8, separated by commas.");
            return;
        }
        const int channel = channelText.getIntValue();
        if (! isPositiveAndBelow (channel - 1, 8))
        {
            showErrorWithDiagnostics ("Invalid channel assignment",
                                      "Each channel number must be from 1 to 8.");
            return;
        }
        channelMask |= (1u << (channel - 1));
    }

    if (! savePluginStates())
        return;
    auto updatedPlugins = activePlugins;
    const auto found = std::find_if (updatedPlugins.begin(), updatedPlugins.end(),
        [&plugin] (const ActivePlugin& active) { return active.instanceId == plugin.instanceId; });
    if (found == updatedPlugins.end())
        return;
    found->channelMask = channelMask;
    if (! persistActivePlugins (updatedPlugins))
        return;
    activePlugins = std::move (updatedPlugins);
    loadActivePlugins();
}

void IconMenu::restoreActivePlugins()
{
    auto* settings = getAppProperties().getUserSettings();
    if (settings->containsKey ("pluginInstances"))
    {
        const auto xml = settings->getXmlValue ("pluginInstances");
        if (xml == nullptr || ! xml->hasTagName ("PLUGININSTANCES"))
        {
            activeChainValid = false;
            showErrorWithDiagnostics ("Could not restore plugin chain",
                                      "The saved instance list is invalid. The original settings have been kept.");
            return;
        }
        StringArray ids;
        for (auto* entry : xml->getChildIterator())
        {
            ActivePlugin instance;
            instance.instanceId = entry->getStringAttribute ("id");
            auto* description = entry->getFirstChildElement();
            if (! entry->hasTagName ("INSTANCE") || instance.instanceId.isEmpty()
                || ids.contains (instance.instanceId) || description == nullptr
                || ! instance.loadFromXml (*description))
            {
                activePlugins.clear();
                activeChainValid = false;
                showErrorWithDiagnostics ("Could not restore plugin chain",
                                          "An instance entry is invalid. The original settings have been kept.");
                return;
            }
            if (entry->hasAttribute ("channelMask"))
            {
                const auto channelMask = entry->getStringAttribute ("channelMask");
                if (! parsePluginChannelMask (channelMask, instance.channelMask))
                {
                    activePlugins.clear();
                    activeChainValid = false;
                    showErrorWithDiagnostics ("Could not restore plugin chain",
                                              "A channel assignment is invalid. The original settings have been kept.");
                    return;
                }
            }
            ids.add (instance.instanceId);
            activePlugins.push_back (std::move (instance));
        }
        return;
    }

    KnownPluginList legacy;
    if (settings->containsKey ("pluginListActive"))
    {
        const auto xml = settings->getXmlValue ("pluginListActive");
        bool valid = xml != nullptr && xml->hasTagName ("KNOWNPLUGINS");
        if (valid)
            for (auto* entry : xml->getChildIterator())
            {
                PluginDescription description;
                if (! entry->hasTagName ("BLACKLISTED") && ! description.loadFromXml (*entry))
                    valid = false;
            }
        if (! valid)
        {
            activeChainValid = false;
            showErrorWithDiagnostics ("Could not restore plugin chain",
                                      "The legacy plugin list is invalid. The original settings have been kept.");
            return;
        }
        legacy.recreateFromXml (*xml);
    }
    auto descriptions = legacy.getTypes();
    std::stable_sort (descriptions.begin(), descriptions.end(), [settings] (const PluginDescription& a, const PluginDescription& b)
    {
        return settings->getValue (getKey ("order", a)).getLargeIntValue()
             < settings->getValue (getKey ("order", b)).getLargeIntValue();
    });
    for (const auto& description : descriptions)
    {
        ActivePlugin instance;
        static_cast<PluginDescription&> (instance) = description;
        instance.instanceId = Uuid().toString();
        for (const auto* type : { "state", "bypass" })
            if (settings->containsKey (getKey (type, description)))
                settings->setValue (getKey (type, instance), settings->getValue (getKey (type, description)));
        activePlugins.push_back (std::move (instance));
    }
    persistActivePlugins (activePlugins);
}

bool IconMenu::persistActivePlugins (const std::vector<ActivePlugin>& plugins)
{
    XmlElement xml ("PLUGININSTANCES");
    for (const auto& instance : plugins)
    {
        auto* entry = xml.createNewChildElement ("INSTANCE");
        entry->setAttribute ("id", instance.instanceId);
        entry->setAttribute ("channelMask", (int) instance.channelMask);
        entry->addChildElement (instance.createXml().release());
    }
    auto* settings = getAppProperties().getUserSettings();
    std::unique_ptr<XmlElement> previousXml (settings->getXmlValue ("pluginInstances"));
    settings->setValue ("pluginInstances", &xml);
    if (! settings->saveIfNeeded())
    {
        if (previousXml != nullptr)
            settings->setValue ("pluginInstances", previousXml.get());
        else
            settings->removeValue ("pluginInstances");
        showErrorWithDiagnostics ("Could not save plugin chain", "The active plugin list could not be saved.");
        return false;
    }
    return true;
}

void IconMenu::deletePluginStates()
{
	auto list = getTimeSortedList();
    for (int i = 0; i < (int) list.size(); i++)
    {
		String pluginUid = getKey("state", list[i]);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
        for (int channel = 1; channel <= 8; ++channel)
            getAppProperties().getUserSettings()->removeValue (pluginUid + "-channel-" + String (channel));
    }
    if (! getAppProperties().saveIfNeeded())
        showErrorWithDiagnostics ("Could not delete plugin states", "The plugin states could not be removed from settings.");
}

bool IconMenu::savePluginStates()
{
	auto list = getTimeSortedList();
    const bool independentChannels = usesIndependentChannelRouting();
    const auto* device = deviceManager.getCurrentAudioDevice();
    const int channelCount = independentChannels && device != nullptr
                                 ? jmin (8, device->getActiveInputChannels().countNumberOfSetBits(),
                                         device->getActiveOutputChannels().countNumberOfSetBits()) : 1;
    for (int i = 0; i < (int) list.size(); i++)
    {
        for (int channel = 0; channel < jmax (1, channelCount); ++channel)
        {
            auto* node = graph.getNodeForId (getPluginNodeId (i, channel, independentChannels));
            if (node == nullptr)
                continue;
            MemoryBlock savedStateBinary;
            node->getProcessor()->getStateInformation (savedStateBinary);
            getAppProperties().getUserSettings()->setValue (
                getInstanceStateKey (list[(size_t) i], channel, independentChannels),
                savedStateBinary.toBase64Encoding());
        }
    }
    const bool saved = getAppProperties().saveIfNeeded();
    if (! saved)
        showErrorWithDiagnostics ("Could not save plugin states", "One or more plugin states could not be saved.");
    return saved;
}

void IconMenu::showAudioSettings()
{
    if (! savePluginStates())
        return;
    std::unique_ptr<XmlElement> previousAudioState (deviceManager.createStateXml());
    auto* settings = getAppProperties().getUserSettings();
    std::unique_ptr<XmlElement> previousSavedAudioState (settings->getXmlValue ("audioDeviceState"));
    AudioDeviceSelectorComponent audioSettingsComp (deviceManager, 0, 8, 0, 8, false, false, true, true);
    audioSettingsComp.setSize(500, 450);
    
    DialogWindow::LaunchOptions o;
    o.content.setNonOwned(&audioSettingsComp);
    o.dialogTitle                   = "Audio Settings";
    o.componentToCentreAround       = this;
    o.dialogBackgroundColour        = Colour::fromRGB(236, 236, 236);
    o.escapeKeyTriggersCloseButton  = true;
    o.useNativeTitleBar             = true;
    o.resizable                     = true;

    audioSettingsDialogOpen = true;
    o.runModal();
    audioSettingsDialogOpen = false;
        
    std::unique_ptr<XmlElement> audioState(deviceManager.createStateXml());
    if (audioState == nullptr)
    {
        if (previousAudioState != nullptr)
        {
            const auto restoreError = deviceManager.initialise (8, 8, previousAudioState.get(), false);
            if (restoreError.isNotEmpty())
                showErrorWithDiagnostics ("Could not restore audio settings", restoreError);
        }
        lastAudioDeviceFingerprint = getAudioDeviceFingerprint();
        loadActivePlugins();
        showErrorWithDiagnostics ("Could not save audio settings", "The audio device manager returned no settings.");
        return;
    }
    settings->setValue("audioDeviceState", audioState.get());
    if (! settings->saveIfNeeded())
    {
        if (previousSavedAudioState != nullptr)
            settings->setValue ("audioDeviceState", previousSavedAudioState.get());
        else
            settings->removeValue ("audioDeviceState");
        if (previousAudioState != nullptr)
        {
            const auto restoreError = deviceManager.initialise (8, 8, previousAudioState.get(), false);
            if (restoreError.isNotEmpty())
                showErrorWithDiagnostics ("Could not restore audio settings", restoreError);
        }
        lastAudioDeviceFingerprint = getAudioDeviceFingerprint();
        loadActivePlugins();
        showErrorWithDiagnostics ("Could not save audio settings", "The selected audio device could not be saved.");
        return;
    }
    lastAudioDeviceFingerprint = getAudioDeviceFingerprint();
    loadActivePlugins();
}

void IconMenu::reloadPlugins()
{
	if (pluginListWindow == nullptr)
		pluginListWindow.reset(new PluginListWindow(*this, formatManager));
	pluginListWindow->toFront(true);
}

void IconMenu::showMainWindow()
{
    if (mainWindow == nullptr)
        mainWindow = std::make_unique<MainWindow> (*this);
    mainWindow->setVisible (true);
    mainWindow->toFront (true);
}

void IconMenu::addPluginFile()
{
    pluginFileChooser = std::make_unique<FileChooser> ("Add plugin file or bundle", File(), "*", false);
    Component::SafePointer<IconMenu> safeThis (this);
    pluginFileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles
                                     | FileBrowserComponent::canSelectDirectories,
        [safeThis] (const FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const auto selected = chooser.getResult();
            if (selected == File())
                return;

            if (! Thread::launch ([safeThis, selected]
            {
                auto candidates = findPluginDescriptionsForFile (selected);
                MessageManager::callAsync ([safeThis, selected, candidates = std::move (candidates)]() mutable
                {
                    if (safeThis == nullptr)
                        return;
                    if (candidates.empty())
                    {
                        showErrorWithDiagnostics ("Plugin not found",
                            "No supported plugin was found in:\n" + selected.getFullPathName());
                        return;
                    }

                    const auto addSelected = [safeThis, candidates] (int selection)
                    {
                        if (safeThis == nullptr || ! isPositiveAndBelow (selection - 1, (int) candidates.size()))
                            return;
                        const auto description = candidates[(size_t) (selection - 1)];
                        const double sampleRate = safeThis->graph.getSampleRate() > 0
                                                      ? safeThis->graph.getSampleRate() : 44100.0;
                        const int blockSize = safeThis->graph.getBlockSize() > 0
                                                  ? safeThis->graph.getBlockSize() : 512;
                        safeThis->formatManager.createPluginInstanceAsync (
                            description, sampleRate, blockSize,
                            [safeThis, description] (std::unique_ptr<AudioPluginInstance> instance, const String& error)
                            {
                                if (safeThis == nullptr)
                                    return;
                                if (instance == nullptr)
                                {
                                    showErrorWithDiagnostics ("Could not load plugin",
                                        description.name + "\n"
                                        + (error.isNotEmpty() ? error : "The selected plugin could not be loaded."));
                                    return;
                                }
                                if (instance->getTotalNumInputChannels() < 1
                                    || instance->getTotalNumOutputChannels() < 1)
                                {
                                    showErrorWithDiagnostics ("Unsupported plugin",
                                        description.name + " does not provide both audio input and output channels.");
                                    return;
                                }
                                instance.reset();
                                if (! safeThis->activeChainValid)
                                {
                                    showErrorWithDiagnostics ("Plugin chain unavailable",
                                        "The saved instance list is invalid. No changes were made; restore a settings backup.");
                                    return;
                                }
                                if (! safeThis->savePluginStates())
                                    return;

                                IconMenu::ActivePlugin active;
                                static_cast<PluginDescription&> (active) = description;
                                active.instanceId = Uuid().toString();
                                auto updatedPlugins = safeThis->activePlugins;
                                updatedPlugins.push_back (std::move (active));
                                if (! safeThis->persistActivePlugins (updatedPlugins))
                                    return;

                                safeThis->knownPluginList.addType (description);
                                safeThis->activePlugins = std::move (updatedPlugins);
                                safeThis->loadActivePlugins();
                                const auto pluginIndex = safeThis->activePlugins.size() - 1;
                                if (pluginIndex < (size_t) safeThis->pluginLoadErrors.size()
                                    && safeThis->pluginLoadErrors[(int) pluginIndex].isNotEmpty())
                                {
                                    showErrorWithDiagnostics ("Plugin added but could not be loaded",
                                        safeThis->pluginLoadErrors[(int) pluginIndex]
                                        + "\nIts saved settings have been kept.");
                                    return;
                                }
                                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Plugin added",
                                    description.name + " was added to the active plugin chain.");
                            });
                    };

                    if (candidates.size() == 1)
                    {
                        addSelected (1);
                        return;
                    }

                    AlertWindow dialog ("Choose plugin", "This file contains multiple plugin types. Select one to add.",
                                        MessageBoxIconType::NoIcon);
                    StringArray names;
                    for (const auto& candidate : candidates)
                        names.add (candidate.name + " (" + candidate.pluginFormatName + ")");
                    dialog.addComboBox ("plugin", names, "Plugin");
                    dialog.addButton ("Add", 1, KeyPress (KeyPress::returnKey));
                    dialog.addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                    if (dialog.runModalLoop() == 1)
                    {
                        const auto* selection = dialog.getComboBoxComponent ("plugin");
                        if (selection != nullptr)
                            addSelected (selection->getSelectedItemIndex() + 1);
                    }
                });
            }))
                showErrorWithDiagnostics ("Plugin scan failed", "Could not start the plugin file scanner.");
        });
}

void IconMenu::checkForUpdates (bool manual)
{
    if (updateInProgress)
    {
        if (manual)
            showErrorWithDiagnostics ("Update in progress", "An update check or download is already running.");
        return;
    }
    updateInProgress = true;
    Component::SafePointer<IconMenu> safeThis (this);
    if (! Thread::launch ([safeThis, manual]
    {
        auto response = readUpdateUrl ("https://annaspannertechsolutions.uk/lighthost/latest", 65536);
        MessageManager::callAsync ([safeThis, manual, response = std::move (response)]() mutable
        {
            if (safeThis == nullptr)
                return;
            safeThis->updateInProgress = false;
            if (response.error.isNotEmpty())
            {
                if (manual)
                    showErrorWithDiagnostics ("Update check failed", response.error);
                return;
            }
            const auto json = JSON::parse (String::fromUTF8 (
                static_cast<const char*> (response.data.getData()), (int) response.data.getSize()));
            const String latest = json.getProperty ("version", {}).toString();
            int currentVersion[3] {}, latestVersion[3] {};
            if (! json.isObject() || ! parseLightHostVersion (latest, latestVersion)
                || ! parseLightHostVersion (ProjectInfo::versionString, currentVersion))
            {
                if (manual)
                    showErrorWithDiagnostics ("Update check failed", "The update manifest contains an invalid version.");
                return;
            }
            const bool newer = isNewerLightHostVersion (latestVersion, currentVersion);
            if (! newer)
            {
                if (manual)
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Light Host updates",
                                                      "You have the latest available version.");
                return;
            }
            const String platform = currentUpdatePlatform();
            String address, hash;
            if (! getLightHostReleaseAsset (json, platform, address, hash))
            {
                if (manual)
                    showErrorWithDiagnostics ("Update unavailable",
                                              "The manifest has no valid installer and SHA-256 for " + platform + ".");
                return;
            }
            AlertWindow::showAsync (MessageBoxOptions()
                .withIconType (MessageBoxIconType::QuestionIcon)
                .withTitle ("Light Host update available")
                .withMessage ("Version " + latest + " is available. Download the verified update from GitHub?")
                .withButton ("Download", 1).withButton ("Not now", 0),
                [safeThis, address, hash, latest] (int result)
                {
                    if (safeThis != nullptr && result == 1)
                        safeThis->downloadUpdate (address, hash, latest);
                });
        });
    }))
    {
        updateInProgress = false;
        if (manual)
            showErrorWithDiagnostics ("Update check failed", "Could not start the update worker.");
    }
}

void IconMenu::downloadUpdate (String url, String sha256, String version)
{
    if (updateInProgress)
    {
        showErrorWithDiagnostics ("Update in progress", "Another update operation is already running.");
        return;
    }
    updateInProgress = true;
    Component::SafePointer<IconMenu> safeThis (this);
    const auto platform = currentUpdatePlatform();
    if (! Thread::launch ([safeThis, url, sha256, version, platform]
    {
        auto response = readUpdateUrl (url, 512u * 1024u * 1024u);
        if (response.error.isEmpty() && ! SHA256 (response.data).toHexString().equalsIgnoreCase (sha256))
            response.error = "The downloaded update's SHA-256 does not match the update manifest.";
        File updateFile;
        if (response.error.isEmpty())
        {
#if JUCE_LINUX
            const auto runningApp = File::getSpecialLocation (File::currentExecutableFile);
            const auto staging = runningApp.getParentDirectory().getNonexistentChildFile (".LightHostUpdate", {});
            if (! staging.createDirectory())
                response.error = "Could not create the update staging directory.";
            else
            {
                const auto extraction = extractLinuxPackage (response.data, staging);
                if (extraction.failed())
                {
                    response.error = extraction.getErrorMessage();
                    if (! staging.deleteRecursively())
                        response.error += "\nCould not remove " + staging.getFullPathName();
                }
                else
                    updateFile = staging.getChildFile ("Light Host");
            }
#elif JUCE_MAC
            updateFile = File::getSpecialLocation (File::tempDirectory)
                             .getNonexistentChildFile ("LightHostUpdate", ".zip");
#else
            updateFile = File::getSpecialLocation (File::tempDirectory)
                             .getNonexistentChildFile ("LightHostSetup", ".exe");
#endif
#if ! JUCE_LINUX
            if (! updateFile.replaceWithData (response.data.getData(), response.data.getSize()))
            {
                response.error = "Could not write the downloaded update to disk.";
                updateFile.deleteFile();
            }
#endif
        }
        MessageManager::callAsync ([safeThis, updateFile, version, platform, error = response.error]
        {
            if (safeThis == nullptr)
            {
                if (updateFile != File())
                {
#if JUCE_LINUX
                    updateFile.getParentDirectory().deleteRecursively();
#else
                    updateFile.deleteFile();
#endif
                }
                return;
            }
            safeThis->updateInProgress = false;
            if (error.isNotEmpty())
            {
                showErrorWithDiagnostics ("Update download failed", error);
                return;
            }
#if JUCE_WINDOWS
            AlertWindow::showAsync (MessageBoxOptions()
                .withIconType (MessageBoxIconType::QuestionIcon)
                .withTitle ("Install Light Host update?")
                .withMessage ("Version " + version + " was downloaded and verified. "
                              "Run its installer now? Light Host will close.")
                .withButton ("Run installer", 1).withButton ("Not now", 0),
                [updateFile] (int result)
                {
                    if (result != 1)
                    {
                        if (! updateFile.deleteFile())
                            showErrorWithDiagnostics ("Could not remove downloaded installer", updateFile.getFullPathName());
                    }
                    else
                    {
                        const auto installDirectory = File::getSpecialLocation (
                            File::currentExecutableFile).getParentDirectory();
                        const auto arguments = "--directory \""
                                             + installDirectory.getFullPathName().replace ("\"", "\\\"")
                                             + "\"";
                        if (updateFile.startAsProcess (arguments))
                            JUCEApplication::getInstance()->quit();
                        else
                        {
                            showErrorWithDiagnostics ("Could not start installer", updateFile.getFullPathName());
                            if (! updateFile.deleteFile())
                                showErrorWithDiagnostics ("Could not remove downloaded installer",
                                                          updateFile.getFullPathName());
                        }
                    }
                });
#else
            const auto runningApp = File::getSpecialLocation (File::currentExecutableFile);
            const auto helper = runningApp.getSiblingFile ("LightHostUpdater");
            if (! helper.existsAsFile())
            {
                showErrorWithDiagnostics ("Update helper unavailable",
                                          "The platform updater is missing beside the application: "
                                              + helper.getFullPathName());
#if JUCE_LINUX
                updateFile.getParentDirectory().deleteRecursively();
#else
                updateFile.deleteFile();
#endif
                return;
            }
            const auto status = updateStatusFile();
#if JUCE_LINUX
            const auto mode = "--linux";
            const auto target = runningApp;
            const auto warning = "The app must be in a writable folder. Light Host will close and reopen to apply the update.";
#else
            const auto mode = "--mac";
            const auto target = runningApp.getParentDirectory().getParentDirectory().getParentDirectory();
            const auto warning = "Light Host will close and reopen to apply the update. macOS may block this unsigned app until it is signed and notarized.";
#endif
            std::vector<String> arguments { mode, String ((int64) getpid()),
                                            updateFile.getFullPathName(), target.getFullPathName(),
                                            status.getFullPathName() };
#if JUCE_LINUX
            arguments.push_back (updateFile.getSiblingFile ("LightHostUpdater").getFullPathName());
#endif
            AlertWindow::showAsync (MessageBoxOptions()
                .withIconType (MessageBoxIconType::QuestionIcon)
                .withTitle ("Install Light Host update?")
                .withMessage ("Version " + version + " was downloaded and its SHA-256 verified. " + warning)
                .withButton ("Install and restart", 1).withButton ("Not now", 0),
                [helper, arguments, updateFile] (int result)
                {
                    if (result != 1)
                    {
#if JUCE_LINUX
                        if (! updateFile.getParentDirectory().deleteRecursively())
#else
                        if (! updateFile.deleteFile())
#endif
                            showErrorWithDiagnostics ("Could not remove downloaded update", updateFile.getFullPathName());
                        return;
                    }
                    if (launchUpdateHelper (helper, arguments))
                        JUCEApplication::getInstance()->quit();
                    else
                    {
                        showErrorWithDiagnostics ("Could not start update helper", helper.getFullPathName());
#if JUCE_LINUX
                        if (! updateFile.getParentDirectory().deleteRecursively())
#else
                        if (! updateFile.deleteFile())
#endif
                            showErrorWithDiagnostics ("Could not remove downloaded update", updateFile.getFullPathName());
                    }
                });
#endif
        });
    }))
    {
        updateInProgress = false;
        showErrorWithDiagnostics ("Update download failed", "Could not start the update worker.");
    }
}

void IconMenu::locatePlugin (ActivePlugin plugin)
{
    const File initial = File::isAbsolutePath(plugin.fileOrIdentifier)
                             ? File(plugin.fileOrIdentifier).getParentDirectory() : File();
    pluginFileChooser = std::make_unique<FileChooser> ("Locate " + plugin.name, initial, "*", false);
    Component::SafePointer<IconMenu> safeThis(this);
    pluginFileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles
                                     | FileBrowserComponent::canSelectDirectories,
        [safeThis, plugin] (const FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const File selected = chooser.getResult();
            if (selected == File())
                return;
            File bundle = selected;
            for (File parent = selected; parent != File(); parent = parent.getParentDirectory())
            {
                if (parent.getFileExtension().equalsIgnoreCase(".vst3"))
                {
                    bundle = parent;
                    break;
                }
                if (parent == parent.getParentDirectory())
                    break;
            }
            if (plugin.pluginFormatName == "VST" && bundle.getFileExtension().equalsIgnoreCase(".vst3"))
            {
                OwnedArray<PluginDescription> candidates;
                for (auto* format : safeThis->formatManager.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile(candidates, bundle.getFullPathName());
                if (candidates.size() != 1)
                {
                    showErrorWithDiagnostics("Could not identify replacement",
                                             "The selected VST3 did not contain exactly one plugin. No settings were changed.");
                    return;
                }
                const PluginDescription replacement = *candidates[0];
                const String replacementWarning = "VST2 settings may not be compatible with VST3. ";
                AlertWindow::showAsync (MessageBoxOptions()
                    .withIconType (MessageBoxIconType::WarningIcon)
                    .withTitle ("Replace VST2 plugin?")
                    .withMessage (replacementWarning + "Replace " + plugin.name + " with " + replacement.name
                                  + " using default settings in the same position? The original saved settings will be kept.")
                    .withButton ("Use defaults", 1).withButton ("Cancel", 0),
                    [safeThis, plugin, replacement] (int result)
                    {
                        if (result == 1 && safeThis != nullptr)
                            safeThis->replacePluginWithDefaults(plugin, replacement);
                    });
                return;
            }
            auto updated = plugin;
            updated.fileOrIdentifier = bundle.getFullPathName();
            String error;
            const double rate = safeThis->graph.getSampleRate() > 0 ? safeThis->graph.getSampleRate() : 44100.0;
            const int size = safeThis->graph.getBlockSize() > 0 ? safeThis->graph.getBlockSize() : 512;
            auto test = safeThis->formatManager.createPluginInstance(updated, rate, size, error);
            if (test == nullptr)
            {
                showErrorWithDiagnostics("Could not load plugin at the selected location",
                                         plugin.name + "\n" + selected.getFullPathName()
                                         + "\n" + (error.isNotEmpty() ? error : "The plugin could not be loaded.")
                                         + "\nThe original location and saved settings were kept.");
                return;
            }
            const auto actual = test->getPluginDescription();
            const bool sameId = plugin.uniqueId != 0 ? actual.uniqueId == plugin.uniqueId
                                                     : actual.deprecatedUid == plugin.deprecatedUid;
            if (! sameId || actual.pluginFormatName != plugin.pluginFormatName)
            {
                showErrorWithDiagnostics("Different plugin selected",
                                         "The selected file does not match " + plugin.name
                                         + ". The original location and saved settings were kept.");
                return;
            }
            test.reset();
            const auto found = std::find_if (safeThis->activePlugins.begin(), safeThis->activePlugins.end(),
                [&plugin] (const ActivePlugin& active)
                {
                    return active.instanceId == plugin.instanceId && active.isDuplicateOf (plugin);
                });
            if (found == safeThis->activePlugins.end())
            {
                showErrorWithDiagnostics("Plugin no longer active",
                                         "The plugin was removed while choosing its location. No settings were changed.");
                return;
            }
            if (! safeThis->savePluginStates())
                return;
            for (const auto& known : safeThis->knownPluginList.getTypes())
                if (known.isDuplicateOf(plugin))
                {
                    safeThis->knownPluginList.removeType(known);
                    safeThis->knownPluginList.addType(updated);
                    break;
                }
            auto updatedPlugins = safeThis->activePlugins;
            const auto updatedFound = std::find_if (updatedPlugins.begin(), updatedPlugins.end(),
                [&plugin] (const ActivePlugin& active) { return active.instanceId == plugin.instanceId; });
            if (updatedFound == updatedPlugins.end())
                return;
            static_cast<PluginDescription&> (*updatedFound) = updated;
            if (! safeThis->persistActivePlugins (updatedPlugins))
                return;
            safeThis->activePlugins = std::move (updatedPlugins);
            safeThis->loadActivePlugins();
        });
}

void IconMenu::replacePluginWithDefaults (ActivePlugin oldPlugin, PluginDescription replacement)
{
    const auto found = std::find_if (activePlugins.begin(), activePlugins.end(),
        [&oldPlugin] (const ActivePlugin& active)
        {
            return active.instanceId == oldPlugin.instanceId && active.isDuplicateOf (oldPlugin);
        });
    if (found == activePlugins.end())
    {
        showErrorWithDiagnostics("Plugin no longer active", "The original plugin was removed. No settings were changed.");
        return;
    }
    String error;
    const double rate = graph.getSampleRate() > 0 ? graph.getSampleRate() : 44100.0;
    const int size = graph.getBlockSize() > 0 ? graph.getBlockSize() : 512;
    auto test = formatManager.createPluginInstance(replacement, rate, size, error);
    if (test == nullptr)
    {
        showErrorWithDiagnostics("Could not load replacement", error + "\nThe original settings were kept.");
        return;
    }
    test.reset();
    if (! savePluginStates())
        return;
    auto* settings = getAppProperties().getUserSettings();
    const String replacementStateKey = getKey ("state", oldPlugin);
    std::vector<std::pair<String, String>> savedStates;
    for (int channel = 0; channel <= 8; ++channel)
    {
        const String stateKey = channel == 0 ? replacementStateKey
                                             : replacementStateKey + "-channel-" + String (channel);
        if (settings->containsKey (stateKey))
            savedStates.emplace_back (stateKey, settings->getValue (stateKey));
    }
    const String backupSuffix = "-backup-" + String (Time::currentTimeMillis());
    for (const auto& state : savedStates)
        settings->setValue (state.first + backupSuffix, state.second);
    if (! savedStates.empty() && ! settings->saveIfNeeded())
    {
        for (const auto& state : savedStates)
            settings->removeValue (state.first + backupSuffix);
        showErrorWithDiagnostics ("Could not save plugin backup", "The replacement was not made.");
        return;
    }
    for (const auto& state : savedStates)
        settings->removeValue (state.first);
    if (! savedStates.empty() && ! settings->saveIfNeeded())
    {
        for (const auto& state : savedStates)
            settings->setValue (state.first, state.second);
        const bool restored = settings->saveIfNeeded();
        showErrorWithDiagnostics ("Could not clear old plugin state",
                                  restored ? "The replacement was not made."
                                           : "The replacement was not made, and the original state could not be restored to the settings file.");
        return;
    }
    auto updatedPlugins = activePlugins;
    const auto updatedFound = std::find_if (updatedPlugins.begin(), updatedPlugins.end(),
        [&oldPlugin] (const ActivePlugin& active) { return active.instanceId == oldPlugin.instanceId; });
    if (updatedFound == updatedPlugins.end())
        return;
    static_cast<PluginDescription&> (*updatedFound) = replacement;
    if (! persistActivePlugins (updatedPlugins))
    {
        for (const auto& state : savedStates)
            settings->setValue (state.first, state.second);
        if (! savedStates.empty() && ! settings->saveIfNeeded())
            showErrorWithDiagnostics ("Could not restore plugin state",
                                      "The original plugin remains active, but its saved state could not be restored. "
                                      "A backup copy is available in Light Host settings.");
        return;
    }
    knownPluginList.addType(replacement);
    activePlugins = std::move (updatedPlugins);
    loadActivePlugins();
    const auto sorted = getTimeSortedList();
    for (int i = 0; i < (int) sorted.size(); ++i)
        if (sorted[(size_t) i].instanceId == oldPlugin.instanceId)
            if (auto* node = graph.getNodeForId(AudioProcessorGraph::NodeID((uint32) (i + 1))))
                if (auto* window = PluginWindow::getWindowFor(node, PluginWindow::Normal))
                    window->toFront(true);
}

void IconMenu::removePluginsLackingInputOutput()
{
	std::vector<int> removeIndex;
	for (int i = 0; i < knownPluginList.getNumTypes(); i++)
	{
		PluginDescription plugin = knownPluginList.getTypes()[i];
		if (plugin.numInputChannels < 1 || plugin.numOutputChannels < 1)
			removeIndex.push_back(i);
	}
	for (int i = 0; i < (int) removeIndex.size(); i++)
		knownPluginList.removeType(knownPluginList.getTypes()[removeIndex[i] - i]);
}