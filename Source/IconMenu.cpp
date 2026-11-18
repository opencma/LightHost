//
//  IconMenu.cpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include "PluginChain.h"
#include "AudioSetup.h"
#include "PluginState.h"
#include "PluginBackup.h"
#include "HostLookAndFeel.h"
#include <algorithm>
#include <ctime>
#include <functional>
#include <limits.h>
#include <string>
#if JUCE_WINDOWS
#include "Windows.h"
#endif

namespace
{
enum
{
	menuOpenLightHost = 1,
	menuRunOnStartup,
	menuQuit
};

enum ActivePluginHoveredAction
{
	noActivePluginAction,
	bypassActivePluginAction,
	menuActivePluginAction
};

const char* const activePluginDragPrefix = "active-plugin:";

#if JUCE_WINDOWS
const wchar_t* const startupRegistryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const startupRegistryValue = L"Light Host";

String getStartupCommandLine()
{
	return File::getSpecialLocation(File::currentExecutableFile)
		.getFullPathName().quoted();
}

void showStartupRegistryError(LSTATUS error)
{
	AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Run on startup",
		"Could not access or update the Windows startup setting. Error code: "
		+ String((int) error));
}

bool getRunOnStartupEnabled(bool& enabled, bool* registered = nullptr)
{
	enabled = false;
	if (registered != nullptr)
		*registered = false;
	HKEY key = nullptr;
	LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, startupRegistryPath, 0,
		KEY_QUERY_VALUE, &key);
	if (result == ERROR_FILE_NOT_FOUND)
		return true;
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return false;
	}

	DWORD type = 0;
	// Run commands are limited to 260 characters, including the quoted path.
	wchar_t commandLine[261] = {};
	DWORD size = sizeof(commandLine);
	result = RegQueryValueExW(key, startupRegistryValue, nullptr, &type,
		reinterpret_cast<BYTE*>(commandLine), &size);
	RegCloseKey(key);
	if (result == ERROR_FILE_NOT_FOUND)
		return true;
	if (registered != nullptr && (result == ERROR_SUCCESS || result == ERROR_MORE_DATA))
		*registered = true;
	if (result == ERROR_MORE_DATA)
		return true;
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return false;
	}

	// Check the command as well as its presence so a moved executable can
	// refresh an existing registration when it launches.
	enabled = type == REG_SZ && size >= sizeof(wchar_t)
		&& size % sizeof(wchar_t) == 0
		&& commandLine[size / sizeof(wchar_t) - 1] == 0
		&& String(commandLine).equalsIgnoreCase(getStartupCommandLine());
	return true;
}

void setRunOnStartupEnabled(bool enabled)
{
	HKEY key = nullptr;
	DWORD disposition = 0;
	LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, startupRegistryPath, 0, nullptr,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, &disposition);
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return;
	}

	if (!enabled)
		result = RegDeleteValueW(key, startupRegistryValue);
	else
	{
		const String executablePath =
			File::getSpecialLocation(File::currentExecutableFile).getFullPathName();
		if (executablePath.isEmpty())
		{
			RegCloseKey(key);
			AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Run on startup",
				"Could not determine the Light Host executable path.");
			return;
		}

		std::wstring commandLine(executablePath.toWideCharPointer());
		commandLine.insert(commandLine.begin(), L'"');
		commandLine.push_back(L'"');
		if (commandLine.length() > 260)
		{
			RegCloseKey(key);
			AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Run on startup",
				"The executable path is too long for Windows startup. Move Light Host "
				"to a folder with a shorter path and enable this setting again.");
			return;
		}
		result = RegSetValueExW(key, startupRegistryValue, 0, REG_SZ,
			reinterpret_cast<const BYTE*>(commandLine.c_str()),
			static_cast<DWORD>((commandLine.length() + 1) * sizeof(wchar_t)));
	}

	RegCloseKey(key);
	if (result != ERROR_SUCCESS && !(!enabled && result == ERROR_FILE_NOT_FOUND))
		showStartupRegistryError(result);
}

void toggleRunOnStartup()
{
	bool enabled = false;
	if (getRunOnStartupEnabled(enabled))
		setRunOnStartupEnabled(!enabled);
}

void refreshStartupRegistration()
{
	bool enabled = false;
	bool registered = false;
	// Absence means the user turned startup off. Only repair existing entries.
	if (getRunOnStartupEnabled(enabled, &registered) && registered && !enabled)
		setRunOnStartupEnabled(true);
}
#endif
}

class LightHostAudioDeviceSelector : public AudioDeviceSelectorComponent
{
public:
    explicit LightHostAudioDeviceSelector (AudioDeviceManager& deviceManager)
        : AudioDeviceSelectorComponent (deviceManager, 0, 2, 0, 2,
                                        false, false, true, false)
    {
    }

    void resized() override
    {
        improveTextReadability (*this);
        AudioDeviceSelectorComponent::resized();

        ComboBox* deviceType = nullptr;
        Component* deviceSettings = nullptr;
        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            Component* child = getChildComponent (i);
            if (auto* combo = dynamic_cast<ComboBox*> (child))
                deviceType = combo;
            else if (child->getWidth() >= getWidth() - 1
                     && child->getHeight() > 100)
                deviceSettings = child;
        }

        if (deviceType != nullptr && deviceSettings != nullptr)
        {
            const int typeTop = 8;
            deviceType->setTopLeftPosition (deviceType->getX(), typeTop);
            deviceSettings->setTopLeftPosition (
                deviceSettings->getX(), deviceType->getBottom() + 12);

            compactAdvancedRowSpacing (*deviceSettings);
        }

        int contentBottom = 0;
        for (int i = 0; i < getNumChildComponents(); ++i)
            contentBottom = jmax (contentBottom, getChildComponent (i)->getBottom());

        const int contentHeight = contentBottom + 8;
        if (contentHeight != getHeight())
            setSize (getWidth(), contentHeight);
    }

private:
    static void improveTextReadability (Component& parent)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            Component& child = *parent.getChildComponent (i);
            if (auto* label = dynamic_cast<Label*> (&child))
            {
                label->setFont (HostDesign::bodyFont (16.0f));
                label->setMinimumHorizontalScale (1.0f);
                if (label->getText() == "Active output channels:")
                    label->setText ("Output channels:", dontSendNotification);
                else if (label->getText() == "Active input channels:")
                    label->setText ("Input channels:", dontSendNotification);
            }
            if (auto* list = dynamic_cast<ListBox*> (&child))
                // JUCE derives channel text size from the row height (60%).
                list->setRowHeight (27);
            improveTextReadability (child);
        }
    }

    static void shiftChildrenFromY (Component& parent, int y, int amount)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            Component* const child = parent.getChildComponent (i);
            if (child->getY() >= y)
                child->setTopLeftPosition (child->getX(), child->getY() - amount);
        }
    }

    void compactAdvancedRowSpacing (Component& deviceSettings)
    {
        int lastChannelListBottom = -1;
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* list = dynamic_cast<ListBox*> (deviceSettings.getChildComponent (i)))
                lastChannelListBottom = jmax (lastChannelListBottom, list->getBottom());

        if (lastChannelListBottom < 0)
            return;

        int sampleRateTop = deviceSettings.getHeight();
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* combo = dynamic_cast<ComboBox*> (deviceSettings.getChildComponent (i)))
                if (combo->getY() >= lastChannelListBottom)
                    sampleRateTop = jmin (sampleRateTop, combo->getY());

        if (sampleRateTop == deviceSettings.getHeight())
            return;

        const int rowSpacing = getItemHeight() / 4;
        shiftChildrenFromY (deviceSettings, sampleRateTop - rowSpacing,
                           rowSpacing * 2);

        int bufferSizeBottom = -1;
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* combo = dynamic_cast<ComboBox*> (deviceSettings.getChildComponent (i)))
                if (combo->getY() >= sampleRateTop)
                    bufferSizeBottom = jmax (bufferSizeBottom, combo->getBottom());

        int buttonsTop = deviceSettings.getHeight();
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* button = dynamic_cast<TextButton*> (deviceSettings.getChildComponent (i)))
                if (button->getY() >= bufferSizeBottom)
                    buttonsTop = jmin (buttonsTop, button->getY());

        if (buttonsTop < deviceSettings.getHeight())
            shiftChildrenFromY (deviceSettings, buttonsTop, rowSpacing);
    }
};

class AudioSettingsViewport : public Viewport
{
public:
    std::function<void(bool, bool)> onScrollStateChanged;

    void visibleAreaChanged (const juce::Rectangle<int>& visibleArea) override
    {
        Component* const content = getViewedComponent();
        if (content != nullptr && onScrollStateChanged)
            onScrollStateChanged (visibleArea.getY() > 0,
                                  visibleArea.getBottom() < content->getHeight());
    }
};

class AudioScrollIndicator : public Component
{
public:
    AudioScrollIndicator()
    {
        setInterceptsMouseClicks (false, false);
    }

    void setScrollState (bool canScrollUp_, bool canScrollDown_)
    {
        if (canScrollUp != canScrollUp_ || canScrollDown != canScrollDown_)
        {
            canScrollUp = canScrollUp_;
            canScrollDown = canScrollDown_;
            repaint();
        }
    }

    void paint (Graphics& g) override
    {
        if (canScrollUp)
        {
            ColourGradient fade (HostDesign::canvas.withAlpha(0.95f), 0.0f, 0.0f,
                                 HostDesign::canvas.withAlpha(0.0f), 0.0f, 22.0f, false);
            g.setGradientFill (fade);
            g.fillRect (getLocalBounds().removeFromTop (22));
        }

        if (canScrollDown)
        {
            const int hintHeight = 28;
            const int hintTop = getHeight() - hintHeight;
            ColourGradient fade (HostDesign::canvas.withAlpha(0.0f), 0.0f, (float) hintTop,
                                 HostDesign::canvas.withAlpha(0.95f), 0.0f, (float) getHeight(), false);
            g.setGradientFill (fade);
            g.fillRect (0, hintTop, getWidth(), hintHeight);

            Path chevron;
            const float x = (float) getWidth() - 12.0f;
            const float y = (float) getHeight() - 13.0f;
            chevron.startNewSubPath (x - 3.0f, y - 2.0f);
            chevron.lineTo (x, y + 1.0f);
            chevron.lineTo (x + 3.0f, y - 2.0f);
            g.setColour(HostDesign::secondary);
            g.strokePath (chevron, PathStrokeType (1.5f, PathStrokeType::curved,
                                                    PathStrokeType::rounded));
        }
    }

private:
    bool canScrollUp = false;
    bool canScrollDown = false;
};

class LightHostSettingsView : public Component,
    private Button::Listener
{
public:
    explicit LightHostSettingsView (IconMenu& owner_)
        : owner (owner_),
          audioDeviceSelector (owner_.getDeviceManager())
    {
        setOpaque (false);
        audioDeviceSelector.setItemHeight (34);
        audioViewport.setViewedComponent (&audioDeviceSelector, false);
        audioViewport.setScrollBarsShown (true, false);
        audioViewport.onScrollStateChanged = [this] (bool canScrollUp, bool canScrollDown)
        {
            audioScrollIndicator.setScrollState (canScrollUp, canScrollDown);
        };
        addAndMakeVisible (audioViewport);
        addAndMakeVisible (audioScrollIndicator);
        restartAudioButton.setButtonText("Restart audio...");
        restartAudioButton.addListener(this);
        addAndMakeVisible(restartAudioButton);

        runOnStartupButton.addListener (this);
        addAndMakeVisible (runOnStartupButton);
        invertIconButton.addListener (this);
        addAndMakeVisible (invertIconButton);
        darkModeButton.setToggleState(HostDesign::isDarkMode(), dontSendNotification);
        darkModeButton.addListener(this);
        addAndMakeVisible(darkModeButton);

        importButton.setButtonText ("Import...");
        importButton.addListener (this);
        addAndMakeVisible (importButton);
        exportButton.setButtonText ("Export...");
        exportButton.addListener (this);
        addAndMakeVisible (exportButton);

        deleteButton.setButtonText ("Delete plugin states...");
        deleteButton.setColour (TextButton::buttonColourId, HostDesign::control);
        deleteButton.setColour (TextButton::textColourOnId, HostDesign::danger);
        deleteButton.setColour (TextButton::textColourOffId, HostDesign::danger);
        deleteButton.addListener (this);
        addAndMakeVisible (deleteButton);

#if !JUCE_WINDOWS
        runOnStartupButton.setEnabled (false);
#endif
        refreshStartupState();

        const String iconColour = getAppProperties().getUserSettings()->getValue ("icon");
        invertIconButton.setToggleState (iconColour.equalsIgnoreCase ("black"),
                                         dontSendNotification);
    }

    void refreshStartupState()
    {
#if JUCE_WINDOWS
        bool enabled = false;
        if (getRunOnStartupEnabled(enabled))
            runOnStartupButton.setToggleState(enabled, dontSendNotification);
#endif
    }

    void lookAndFeelChanged() override
    {
        darkModeButton.setToggleState(HostDesign::isDarkMode(), dontSendNotification);
        deleteButton.setColour(TextButton::buttonColourId, HostDesign::control);
        deleteButton.setColour(TextButton::textColourOnId, HostDesign::danger);
        deleteButton.setColour(TextButton::textColourOffId, HostDesign::danger);
    }

    void paint (Graphics& g) override
    {
        HostDesign::drawPanel (g, audioCard);
        HostDesign::drawPanel (g, appCard);
        g.setColour (HostDesign::text);
        g.setFont (Font (21.0f, Font::bold));
        g.drawText ("Audio", 0, 0, audioCard.getWidth(), 28, Justification::centredLeft);
        g.drawText ("App", appCard.getX(), 0, appCard.getWidth(), 28, Justification::centredLeft);
        g.drawText ("Backup & restore", appCard.getX(), 220, appCard.getWidth(), 28, Justification::centredLeft);
        g.drawText ("Plugin states", appCard.getX(), 374, appCard.getWidth(), 28, Justification::centredLeft);
        g.setColour (HostDesign::border);
        g.drawHorizontalLine (appCard.getY() + 52, (float) appCard.getX() + 16,
                              (float) appCard.getRight() - 16);
        g.drawHorizontalLine (appCard.getY() + 104, (float) appCard.getX() + 16,
                              (float) appCard.getRight() - 16);
        g.drawHorizontalLine (getHeight() - 68, 0.0f, (float) audioCard.getRight());
        g.setColour (HostDesign::secondary);
        g.setFont (HostDesign::bodyFont(16.0f));
        g.drawFittedText ("Keep your settings, plugin states, and available plugin files together.",
            appCard.getX(), 256, appCard.getWidth(), 44, Justification::topLeft, 2, 1.0f);
        g.drawText (".lightbak backup files", appCard.getX(), 342, appCard.getWidth(), 22, Justification::centredLeft);
        g.drawText ("Reset saved settings for your effects.", appCard.getX(), 408,
            appCard.getWidth(), 22, Justification::centredLeft);
        g.drawText ("Up to two input and two output channels.", 0, audioCard.getBottom() + 8,
            audioCard.getWidth(), 20, Justification::centredLeft);
        g.drawFittedText ("Reopen the device and reload effects.\nAudio pauses briefly; plugin editors close.",
            0, getHeight() - 58, audioCard.getWidth() - 150, 52, Justification::centredLeft, 3, 1.0f);
    }

    void resized() override
    {
        juce::Rectangle<int> area (getLocalBounds());
        const int gap = 28;
        const int cardWidth = (area.getWidth() - gap) * 60 / 100;
        audioCard = area.removeFromLeft (cardWidth).withTrimmedTop (36).withTrimmedBottom (108);
        area.removeFromLeft (gap);
        appCard = area.withY (36).withHeight (156);
        restartAudioButton.setBounds (audioCard.getRight() - 136, getHeight() - 52, 136, 34);

        juce::Rectangle<int> audioContents = audioCard.reduced (10);
        audioViewport.setBounds (audioContents);
        audioScrollIndicator.setBounds (audioContents);
        audioDeviceSelector.setSize (
            jmax (0, audioViewport.getWidth() - audioViewport.getScrollBarThickness()),
            jmax (audioViewport.getHeight(), audioDeviceSelector.getHeight()));

        juce::Rectangle<int> appContents = appCard.reduced (16, 0);
        runOnStartupButton.setBounds (appContents.removeFromTop (52));
        invertIconButton.setBounds (appContents.removeFromTop (52));
        darkModeButton.setBounds(appContents);
        juce::Rectangle<int> backupButtons (appCard.getX(), 306, appCard.getWidth(), 34);
        exportButton.setBounds (backupButtons.removeFromLeft ((backupButtons.getWidth() - 10) / 2));
        backupButtons.removeFromLeft (10);
        importButton.setBounds (backupButtons);
        deleteButton.setBounds (appCard.getX(), 440, appCard.getWidth(), 34);
    }

    void buttonClicked (Button* button) override
    {
        if (button == &darkModeButton)
        {
            const bool dark = darkModeButton.getToggleState();
            getAppProperties().getUserSettings()->setValue("darkMode", dark);
            if (auto* theme = dynamic_cast<LightHostLookAndFeel*>(&getLookAndFeel()))
                theme->applyDarkMode(dark);
            return;
        }
        if (button == &restartAudioButton)
        {
            owner.restartAudio();
            return;
        }
        if (button == &runOnStartupButton)
        {
#if JUCE_WINDOWS
            toggleRunOnStartup();
            refreshStartupState();
#endif
            return;
        }

        if (button == &invertIconButton)
        {
            owner.toggleIconColor();
            return;
        }

        if (button == &importButton)
        {
            if (owner.importBackup())
                restartAfterSettingsImport();
            return;
        }

        if (button == &exportButton)
        {
            owner.exportBackup();
            return;
        }

        if (button == &deleteButton)
        {
            if (AlertWindow::showOkCancelBox (AlertWindow::WarningIcon, "Delete plugin states",
                "Reset all active effects to their default settings? This closes plugin editors and briefly interrupts audio.",
                "Reset effects", "Cancel", this))
            {
                owner.deletePluginStates();
                owner.loadActivePlugins();
            }
        }
    }

private:
    IconMenu& owner;
    LightHostAudioDeviceSelector audioDeviceSelector;
    AudioSettingsViewport audioViewport;
    AudioScrollIndicator audioScrollIndicator;
    HostDesign::Switch runOnStartupButton {"Run on startup"};
    HostDesign::Switch invertIconButton {"Invert tray icon"};
    HostDesign::Switch darkModeButton {"Dark mode"};
    TextButton importButton, exportButton, deleteButton, restartAudioButton;
    juce::Rectangle<int> audioCard, appCard;
};

class LightHostAboutView : public Component
{
public:
    LightHostAboutView()
    {
        title.setText ("Light Host", dontSendNotification);
        title.setFont (Font (26.0f, Font::bold));
        title.setJustificationType (Justification::centred);
        addAndMakeVisible (title);

        version.setText ("Version " + String (ProjectInfo::versionString), dontSendNotification);
        version.setFont (HostDesign::bodyFont(15.0f));
        version.setJustificationType (Justification::centred);
        version.setColour (Label::textColourId, HostDesign::secondary);
        addAndMakeVisible (version);

        credit.setText ("A lightweight stereo effects host.\nBuilt with JUCE. Created by Rolando Islas.", dontSendNotification);
        credit.setJustificationType (Justification::centred);
        credit.setFont (HostDesign::bodyFont(15.0f));
        credit.setColour (Label::textColourId, HostDesign::secondary);
        addAndMakeVisible (credit);
    }

    void lookAndFeelChanged() override
    {
        version.setColour(Label::textColourId, HostDesign::secondary);
        credit.setColour(Label::textColourId, HostDesign::secondary);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (HostDesign::canvas);
        HostDesign::drawSymbol (g, HostDesign::waveform,
            juce::Rectangle<float> ((float) getWidth() / 2 - 26, 48, 52, 52), HostDesign::text);
    }

    void resized() override
    {
        title.setBounds (0, 128, getWidth(), 38);
        version.setBounds (0, 172, getWidth(), 26);
        credit.setBounds (24, 222, getWidth() - 48, 60);
    }

private:
    Label title, version, credit;
};

#include "PluginLibraryView.h"

class IconMenu::PluginManagementComponent : public Component,
	public DragAndDropContainer, public DragAndDropTarget,
	private ListBoxModel, private ChangeListener
{
public:
	PluginManagementComponent(IconMenu& owner_)
		: owner(owner_), activePlugins("Signal chain", this),
		  library(owner_.knownPluginList, owner_.activePluginList),
		  hoveredRow(-1), hoveredAction(noActivePluginAction), dragTargetIndex(-1)
	{
		activePlugins.setRowHeight(HostDesign::effectRowHeight);
		activePlugins.setOutlineThickness(0);
		activePlugins.setColour(ListBox::backgroundColourId, HostDesign::canvas);
		activePlugins.setMultipleSelectionEnabled(false);
		activePlugins.onKeyPress = [this] (const KeyPress& key) { return handleChainKey(key); };
		addAndMakeVisible(activePlugins);
		activePlugins.addMouseListener(this, true);

		addAndMakeVisible(library);
		library.onAddPlugin = [this] (const PluginDescription& plugin)
		{
			owner.addPluginToActiveList(plugin);
			refreshActivePlugins();
		};
		library.configureScanning(owner.formatManager, *getAppProperties().getUserSettings());
		owner.deviceManager.addChangeListener(this);

		activeEmptyState.setText(
			"Your signal chain starts here.\nAdd an effect from the plugin library.",
			dontSendNotification);
		activeEmptyState.setJustificationType(Justification::centred);
		activeEmptyState.setColour(Label::textColourId, HostDesign::secondary);
		activeEmptyState.setFont(HostDesign::bodyFont(15.0f));
		activeEmptyState.setInterceptsMouseClicks(false, false);
		addAndMakeVisible(activeEmptyState);

		refreshActivePlugins();
	}

	~PluginManagementComponent() override
	{
		owner.deviceManager.removeChangeListener(this);
		activePlugins.setModel(nullptr);
	}

	void focusLibrary() { library.focusSearch(); }
	void importPlugins() { library.importPlugins(); }

	void lookAndFeelChanged() override
	{
		activePlugins.setColour(ListBox::backgroundColourId, HostDesign::canvas);
		activeEmptyState.setColour(Label::textColourId, HostDesign::secondary);
	}

	void refreshActivePlugins()
	{
		activePluginRows = owner.getOrderedActivePlugins();
		activePlugins.updateContent();
		activeEmptyState.setVisible(owner.activePluginList.getNumTypes() == 0);
		library.refreshInstanceCounts();
		resized();
		activePlugins.repaint();
		repaint();
	}

	void mouseUp(const MouseEvent&) override
	{
		dragHandleRow = -1;
	}

	void mouseDown(const MouseEvent& event) override
	{
		if (! activePlugins.isParentOf(event.eventComponent)
			&& event.eventComponent != &activePlugins)
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		const int row = activePlugins.getRowContainingPosition(listEvent.x, listEvent.y);
		dragHandleRow = event.mods.isLeftButtonDown()
			&& isDragHandlePosition(listEvent.getPosition()) ? row : -1;
	}

	void mouseMove(const MouseEvent& event) override
	{
		updateActiveHover(event);
	}

	void mouseExit(const MouseEvent& event) override
	{
		updateActiveHover(event);
	}

	void resized() override
	{
		if (getWidth() <= 0 || getHeight() <= 0) return;
		const int gap = 48;
		const int chainWidth = (getWidth() - gap) * 58 / 100;
		const int availableHeight = jmax(HostDesign::effectRowHeight, getHeight() - 184);
		const int rowsHeight = activePluginRows.empty() ? jmin(160, availableHeight)
			: jmin((int) activePluginRows.size() * HostDesign::effectRowHeight, availableHeight);
		inputBounds = juce::Rectangle<int>(40, 0, chainWidth - 44, 56);
		activePlugins.setBounds(0, 72, chainWidth, rowsHeight);
		activeEmptyState.setBounds(activePlugins.getBounds().withTrimmedLeft(40));
		outputBounds = inputBounds.withY(activePlugins.getBottom() + 12);
		library.setBounds(chainWidth + gap, 0, getWidth() - chainWidth - gap, getHeight());
	}

	void paint(Graphics& g) override
	{
		g.setColour(HostDesign::border);
		g.drawVerticalLine(library.getX() - 24, 0, (float) getHeight());
		g.drawVerticalLine(60, (float) inputBounds.getBottom(), (float) outputBounds.getY());
		paintEndpoint(g, inputBounds, true);
		paintEndpoint(g, outputBounds, false);
		g.setColour(HostDesign::secondary);
		g.setFont(HostDesign::bodyFont(15.0f));
		g.drawFittedText("Drag to reorder / Double-click or Enter to edit\nSpace to bypass / Ctrl+Up or Ctrl+Down to reorder",
			0, getHeight() - 34, activePlugins.getWidth(), 34, Justification::centredLeft, 2);
	}

	bool handleChainKey(const KeyPress& key)
	{
		const int row = activePlugins.getSelectedRow();
		if (!isPositiveAndBelow(row, (int) activePluginRows.size())) return false;
		if (key.getKeyCode() == KeyPress::spaceKey)
		{
			owner.performPluginAction(owner.INDEX_BYPASS + row);
			return true;
		}
		if (key.getKeyCode() == KeyPress::F2Key) { renamePlugin(row); return true; }
		if (key.getModifiers().isCtrlDown()
			&& (key.getKeyCode() == KeyPress::upKey || key.getKeyCode() == KeyPress::downKey))
		{
			const int target = jlimit(0, (int) activePluginRows.size() - 1,
				row + (key.getKeyCode() == KeyPress::upKey ? -1 : 1));
			owner.moveActivePlugin(row, target);
			activePlugins.selectRow(target);
			return true;
		}
		return false;
	}

private:
	void changeListenerCallback(ChangeBroadcaster*) override { repaint(); }

	void paintEndpoint(Graphics& g, juce::Rectangle<int> endpointBounds, bool isInput)
	{
		HostDesign::drawPanel(g, endpointBounds);
		HostDesign::drawSymbol(g, isInput ? HostDesign::input : HostDesign::output,
			juce::Rectangle<float>((float) endpointBounds.getX() + 14, (float) endpointBounds.getY() + 17, 22, 22), HostDesign::secondary);
		AudioDeviceManager::AudioDeviceSetup setup;
		owner.deviceManager.getAudioDeviceSetup(setup);
		const String name = isInput ? setup.inputDeviceName : setup.outputDeviceName;
		const BigInteger channels = isInput ? setup.inputChannels : setup.outputChannels;
		StringArray channelNames;
		for (int bit = channels.findNextSetBit(0); bit >= 0; bit = channels.findNextSetBit(bit + 1))
			channelNames.add(String(bit + 1));
		const int textLeft = endpointBounds.getX() + 52, width = endpointBounds.getWidth() - 62;
		g.setColour(HostDesign::secondary);
		g.setFont(Font(12.0f, Font::bold));
		g.drawText(isInput ? "INPUT" : "OUTPUT", textLeft, endpointBounds.getY() + 2, width, 15, Justification::centredLeft);
		g.setColour(HostDesign::text);
		g.setFont(Font(15.5f, Font::bold));
		g.drawText(name.isEmpty() ? "No device selected" : name, textLeft, endpointBounds.getY() + 17, width, 21, Justification::centredLeft);
		g.setColour(HostDesign::secondary);
		g.setFont(HostDesign::bodyFont(15.0f));
		g.drawText(channelNames.isEmpty() ? "No active channels" : "Channels " + channelNames.joinIntoString(", "),
			textLeft, endpointBounds.getY() + 36, width, 18, Justification::centredLeft);
	}

	void returnKeyPressed(int row) override { owner.performPluginAction(owner.INDEX_EDIT + row); }
	int getNumRows() override
	{
		return (int) activePluginRows.size();
	}

	void paintListBoxItem(int row, Graphics& g, int width, int height, bool selected) override
	{
		if (!isPositiveAndBelow(row, (int) activePluginRows.size())) return;
		const auto& plugin = activePluginRows[row];
		const bool bypassed = getAppProperties().getUserSettings()->getBoolValue(IconMenu::getKey("bypass", plugin));
		const bool available = isPositiveAndBelow(row, (int) owner.loadedPlugins.size())
			&& owner.graph.getNodeForId(owner.loadedPlugins[row].nodeId) != nullptr;
		g.fillAll(HostDesign::canvas);
		g.setColour(HostDesign::border);
		g.drawVerticalLine(60, 0, (float) height);
		const juce::Rectangle<float> card(40, 6, (float) width - 44, (float) height - 12);
		g.setColour(selected ? HostDesign::selection : (!HostDesign::isDarkMode() && row == hoveredRow) ? HostDesign::surface : HostDesign::canvas);
		g.fillRoundedRectangle(card, 10);
		g.setColour(selected ? HostDesign::accent : HostDesign::border);
		g.drawRoundedRectangle(card.reduced(0.5f), 10, 1);
		g.setColour(HostDesign::secondary);
		g.setFont(HostDesign::bodyFont(15.0f));
		g.drawText(String(row + 1).paddedLeft('0', 2), 0, 0, 28, height, Justification::centredLeft);
		for (int dotRow = 0; dotRow < 3; ++dotRow)
			for (int dotColumn = 0; dotColumn < 2; ++dotColumn)
				g.fillEllipse(53.0f + dotColumn * 4, height * 0.5f - 6 + dotRow * 5, 2, 2);
		HostDesign::drawSymbol(g, HostDesign::effect, juce::Rectangle<float>(76, height * 0.5f - 12, 24, 24),
			available ? HostDesign::secondary : HostDesign::warning);
		g.setColour(bypassed ? HostDesign::secondary : HostDesign::text);
		g.setFont(Font(17.0f, Font::bold));
		g.drawText(getDisplayNameForPlugin(plugin), 114, 9, width - 208, 22, Justification::centredLeft);
		g.setColour(available ? HostDesign::secondary : HostDesign::warning);
		g.setFont(HostDesign::bodyFont(15.0f));
		g.drawText(plugin.pluginFormatName + " / " + HostDesign::effectStatus(available, bypassed),
			114, 32, width - 208, 20, Justification::centredLeft);
		const auto powerBounds = getBypassBounds(width, height).toFloat();
		HostDesign::drawPowerButton(g, powerBounds, available, bypassed,
			row == hoveredRow && hoveredAction == bypassActivePluginAction);
		const auto menuBounds = getMenuBounds(width, height).toFloat();
		if (row == hoveredRow && hoveredAction == menuActivePluginAction)
		{
			g.setColour(HostDesign::border);
			g.fillRoundedRectangle(menuBounds, 7);
		}
		HostDesign::drawSymbol(g, HostDesign::more, menuBounds.reduced(5), HostDesign::secondary);
		if (dragTargetIndex == row || (row == (int) activePluginRows.size() - 1
			&& dragTargetIndex == (int) activePluginRows.size()))
		{
			g.setColour(HostDesign::accent);
			g.fillRect(40, dragTargetIndex == row ? 0 : height - 2, width - 44, 2);
		}
	}
	void listBoxItemClicked(int row, const MouseEvent& event) override
	{
		if (event.getNumberOfClicks() > 1)
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		if (row < 0 || row >= owner.activePluginList.getNumTypes())
			return;

		if (event.mods.isPopupMenu())
		{
			activePlugins.selectRow(row, true, true);
			showPluginMenu (row, event.getScreenPosition());
			return;
		}

		const ActivePluginHoveredAction action =
			getActionAtPosition(listEvent.getPosition(), row);
		if (action == menuActivePluginAction)
		{
			activePlugins.selectRow (row, true, true);
			const juce::Rectangle<int> menuAnchor (event.getScreenPosition().x,
				event.getScreenPosition().y, 1, 1);
			showPluginMenu (row, menuAnchor.getPosition());
			return;
		}

		if (action == bypassActivePluginAction)
		{
			owner.performPluginAction(owner.INDEX_BYPASS + row);
			refreshActivePlugins();
		}
	}

	void deleteKeyPressed(int row) override
	{
		confirmRemovePlugin(row);
	}

	void confirmRemovePlugin(int row)
	{
		const std::vector<PluginInstance> plugins = owner.getOrderedActivePlugins();
		if (row < 0 || row >= (int) plugins.size())
			return;

		if (AlertWindow::showOkCancelBox(AlertWindow::QuestionIcon,
			"Remove Plugin", "Remove " + getDisplayNameForPlugin (plugins[row])
				+ " from your active plugins?",
			"Remove", "Cancel"))
		{
			owner.performPluginAction(owner.INDEX_DELETE + row);
			refreshActivePlugins();
		}
	}

	void showPluginMenu (int row, Point<int> screenPosition)
	{
		if (row < 0 || row >= owner.activePluginList.getNumTypes())
			return;

		const String identity = IconMenu::getKey("order", activePluginRows[row]);
		PopupMenu actions;
		actions.addItem(1, "Rename");
		actions.addSeparator();
		actions.addColouredItem(2, "Remove", HostDesign::danger);
		Component::SafePointer<PluginManagementComponent> safe(this);
		actions.showMenuAsync(PopupMenu::Options()
			.withTargetScreenArea(juce::Rectangle<int>(screenPosition.x, screenPosition.y, 1, 1))
			.withMinimumWidth(160), ModalCallbackFunction::create([safe, identity](int action) mutable
			{
				if (safe == nullptr || action == 0) return;
				const auto current = safe->owner.getOrderedActivePlugins();
				int row = -1;
				for (int i = 0; i < (int) current.size(); ++i)
					if (identity == IconMenu::getKey("order", current[i])) { row = i; break; }
				if (row < 0) return;
				if (action == 1)
					safe->renamePlugin(row);
				else if (action == 2)
					safe->confirmRemovePlugin(row);
			}));
	}

	void renamePlugin (int row)
	{
		const std::vector<PluginInstance> plugins = owner.getOrderedActivePlugins();
		if (row < 0 || row >= (int) plugins.size())
			return;

		const PluginInstance& plugin = plugins[row];
		AlertWindow renameDialog ("Rename plugin", "Choose a display name for this plugin.",
			AlertWindow::NoIcon, this);
		renameDialog.addTextEditor ("name", getDisplayNameForPlugin (plugin), "Name:");
		renameDialog.addButton ("Save", 1, KeyPress (KeyPress::returnKey));
		renameDialog.addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
		if (renameDialog.runModalLoop() != 1)
			return;

		const String newName = renameDialog.getTextEditorContents ("name").trim();
		if (newName.isEmpty())
			return;

		PropertiesFile* settings = getAppProperties().getUserSettings();
		const String key = getDisplayNameKey (plugin);
		if (newName == plugin.name)
			settings->removeValue (key);
		else
			settings->setValue (key, newName);
		settings->saveIfNeeded();
		for (const auto& loaded : owner.loadedPlugins)
			if (loaded.description.instanceId == plugin.instanceId)
				PluginWindow::setTitleFor(owner.graph.getNodeForId(loaded.nodeId), getDisplayNameForPlugin(plugin));
		activePlugins.repaint();
	}

	void listBoxItemDoubleClicked(int row, const MouseEvent& event) override
	{
		if (row < 0 || row >= owner.activePluginList.getNumTypes())
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		if (getActionAtPosition(listEvent.getPosition(), row) == noActivePluginAction)
			owner.performPluginAction(owner.INDEX_EDIT + row);
	}

	var getDragSourceDescription(const SparseSet<int>& rowsToDescribe) override
	{
		if (rowsToDescribe.size() != 1 || dragHandleRow != rowsToDescribe[0])
			return var();

		const int row = rowsToDescribe[0];
		const std::vector<PluginInstance> plugins = owner.getOrderedActivePlugins();
		if (row < 0 || row >= (int) plugins.size())
			return var();

		return String(activePluginDragPrefix) + IconMenu::getKey("order", plugins[row]);
	}

	bool isInterestedInDragSource(const SourceDetails& details) override
	{
		return details.sourceComponent.get() == &activePlugins
			&& details.description.toString().startsWith(activePluginDragPrefix);
	}

	void itemDragEnter(const SourceDetails& details) override
	{
		updateDragTarget(details);
	}

	void itemDragMove(const SourceDetails& details) override
	{
		updateDragTarget(details);
	}

	void itemDragExit(const SourceDetails&) override
	{
		setDragTargetIndex(-1);
	}

	void itemDropped(const SourceDetails& details) override
	{
		const String description = details.description.toString();
		if (!description.startsWith(activePluginDragPrefix))
		{
			setDragTargetIndex(-1);
			return;
		}

		const String pluginOrderKey = description.substring(
			String(activePluginDragPrefix).length());
		const std::vector<PluginInstance> plugins = owner.getOrderedActivePlugins();
		int fromIndex = -1;
		for (int i = 0; i < (int) plugins.size(); ++i)
			if (pluginOrderKey == IconMenu::getKey("order", plugins[i]))
			{
				fromIndex = i;
				break;
			}

		if (fromIndex >= 0 && !plugins.empty())
		{
			const Point<int> listPosition =
				activePlugins.getLocalPoint(this, details.localPosition);
			int toIndex = getInsertionIndex(listPosition);
			if (toIndex > fromIndex)
				--toIndex;
			toIndex = jlimit(0, (int) plugins.size() - 1, toIndex);
			owner.moveActivePlugin(fromIndex, toIndex);
			activePlugins.selectRow(toIndex, true, true);
			refreshActivePlugins();
		}

		setDragTargetIndex(-1);
	}

	void updateActiveHover(const MouseEvent& event)
	{
		if (!activePlugins.isParentOf(event.eventComponent)
			&& event.eventComponent != &activePlugins)
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		const bool insideList = activePlugins.getLocalBounds().contains(listEvent.getPosition());
		const int row = insideList
			? activePlugins.getRowContainingPosition(listEvent.x, listEvent.y) : -1;
		const ActivePluginHoveredAction action = row >= 0
			? getActionAtPosition(listEvent.getPosition(), row) : noActivePluginAction;
		if (row == hoveredRow && action == hoveredAction)
			return;

		const int previousRow = hoveredRow;
		hoveredRow = row;
		hoveredAction = action;
		if (previousRow >= 0)
			activePlugins.repaint(activePlugins.getRowPosition(previousRow, true));
		if (hoveredRow >= 0)
			activePlugins.repaint(activePlugins.getRowPosition(hoveredRow, true));
	}

	juce::Rectangle<int> getBypassBounds(int width, int height) const
	{
		return juce::Rectangle<int>(width - 82, (height - 32) / 2, 32, 32);
	}

	juce::Rectangle<int> getMenuBounds (int width, int height) const
	{
		return juce::Rectangle<int> (width - 42, (height - 32) / 2, 32, 32);
	}

	bool isDragHandlePosition (Point<int> position) const
	{
		return position.x >= 40 && position.x < 72
			&& activePlugins.getRowContainingPosition (position.x, position.y) >= 0;
	}

	ActivePluginHoveredAction getActionAtPosition(Point<int> position, int row) const
	{
		const juce::Rectangle<int> rowBounds = activePlugins.getRowPosition(row, true);
		const Point<int> rowPosition(position.x - rowBounds.getX(),
			position.y - rowBounds.getY());
		if (getBypassBounds(rowBounds.getWidth(), rowBounds.getHeight())
			.contains(rowPosition))
			return bypassActivePluginAction;
		if (getMenuBounds (rowBounds.getWidth(), rowBounds.getHeight())
			.contains (rowPosition))
			return menuActivePluginAction;
		return noActivePluginAction;
	}

	String getDisplayNameKey (const PluginInstance& plugin) const
	{
		return plugin.settingsKey("display-name");
	}

	String getDisplayNameForPlugin (const PluginInstance& plugin) const
	{
		return getAppProperties().getUserSettings()->getValue (
			getDisplayNameKey (plugin), plugin.name);
	}

	int getInsertionIndex(const Point<int>& position) const
	{
		const int numRows = owner.activePluginList.getNumTypes();
		if (numRows == 0 || position.y < 0)
			return 0;
		if (position.y >= activePlugins.getHeight())
			return numRows;

		const int row = activePlugins.getRowContainingPosition(position.x, position.y);
		if (row < 0)
			return numRows;

		const juce::Rectangle<int> rowBounds = activePlugins.getRowPosition(row, true);
		return row + (position.y >= rowBounds.getCentreY() ? 1 : 0);
	}

	void setDragTargetIndex(int index)
	{
		if (dragTargetIndex != index)
		{
			dragTargetIndex = index;
			activePlugins.repaint();
		}
	}

	void updateDragTarget(const SourceDetails& details)
	{
		const Point<int> listPosition = activePlugins.getLocalPoint(this, details.localPosition);
		setDragTargetIndex(getInsertionIndex(listPosition));
	}

	IconMenu& owner;
	Label activeEmptyState;
	std::vector<PluginInstance> activePluginRows;
	HostDesign::ActionListBox activePlugins;
	PluginLibraryView library;
	juce::Rectangle<int> inputBounds, outputBounds;
	int hoveredRow;
	ActivePluginHoveredAction hoveredAction;
	int dragTargetIndex;
	int dragHandleRow = -1;
};

#include "HostWindowContent.h"

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
	explicit PluginListWindow(IconMenu& owner_)
		: DocumentWindow("Light Host", HostDesign::canvas,
			DocumentWindow::allButtons),
		owner(owner_),
		content(new LightHostMainWindowContent(owner_))
	{
		setContentOwned(content, true);
		setUsingNativeTitleBar(true);
		setResizable(true, false);
		setResizeLimits(1000, 660, 10000, 10000);
		centreWithSize(1180, 780);
		restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("mainWindowState"));
		setVisible(true);
		HostDesign::updateNativeTitleBar(*this);
		restoringWindowState = false;
	}

    ~PluginListWindow() override { saveWindowState(); }

    void moved() override
    {
        DocumentWindow::moved();
        saveWindowState();
    }

    void resized() override
    {
        DocumentWindow::resized();
        saveWindowState();
    }

    void lookAndFeelChanged() override
    {
        DocumentWindow::lookAndFeelChanged();
        setBackgroundColour(HostDesign::canvas);
        HostDesign::updateNativeTitleBar(*this);
    }

	void refreshActivePlugins()
	{
		if (content != nullptr)
			content->refreshActivePlugins();
	}

	void showPluginsView()
	{
		if (content != nullptr)
			content->setView(LightHostMainWindowContent::pluginsView);
		setVisible(true);
		toFront(true);
	}

    void refreshStartupState()
    {
        if (content != nullptr) content->refreshStartupState();
    }

	void closeButtonPressed() override
	{
		saveWindowState();
		setVisible(false);
	}

    void visibilityChanged() override
    {
        DocumentWindow::visibilityChanged();
        // Minimized and tray windows both leave the audio engine running.
        if (content != nullptr) content->setStatusUpdatesEnabled(isVisible() && !isMinimised());
    }

private:
	void saveWindowState()
	{
		// Settings batches move/resize writes. Preserve imported settings during restart.
		if (!restoringWindowState && !owner.settingsImported)
			getAppProperties().getUserSettings()->setValue("mainWindowState", getWindowStateAsString());
	}

	IconMenu& owner;
	LightHostMainWindowContent* content; // Owned by DocumentWindow.
	bool restoringWindowState = true;
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000),
    player(getAppProperties().getUserSettings()->getFile().withFileExtension("audio.log"))
{
    // Initialization
	settingsImported = false;
	PluginState::relocatePortableSettings(*getAppProperties().getUserSettings(),
		File::getSpecialLocation(File::currentExecutableFile).getParentDirectory());
	lastLeftClickTime = 0;
    formatManager.addDefaultFormats();
	#if JUCE_WINDOWS
	refreshStartupRegistration();
	x = y = 0;
	#endif
    // Audio device
    ScopedPointer<XmlElement> savedAudioState (getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    if (savedAudioState != nullptr)
        AudioSetup::limitSavedChannelsToStereo(*savedAudioState);
    const String audioError = deviceManager.initialise(2, 2, savedAudioState, true);
    if (audioError.isNotEmpty())
        player.audioDeviceError(audioError);
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    // Plugins - all
    ScopedPointer<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    knownPluginList.addChangeListener(this);
    // Plugins - active
    ScopedPointer<XmlElement> savedPluginListActive(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
    if (savedPluginListActive != nullptr)
        activePluginList.recreateFromXml(*savedPluginListActive);
    PluginState::migrateLegacySettings(*getAppProperties().getUserSettings(),
        PluginState::collectPlugins(knownPluginList, activePluginList));
    activePluginList.migrateLegacySettings(*getAppProperties().getUserSettings());
    loadActivePlugins();
    activePluginList.addChangeListener(this);
	setIcon();
};

IconMenu::~IconMenu()
{
	deviceManager.removeAudioCallback(&player);
	PluginWindow::closeAllCurrentlyOpenWindows();
	pluginListWindow = nullptr;
    if (!settingsImported)
    {
        ScopedPointer<XmlElement> audioState(deviceManager.createStateXml());
        getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState);
    }
	savePluginStates();
	player.setProcessor(nullptr);
}

void IconMenu::setIcon()
{
	// Set menu icon
	#if JUCE_MAC
		if (exec("defaults read -g AppleInterfaceStyle").compare("Dark") == 1)
			setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize));
		else
			setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
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
		setIconImage(icon);
	#endif
}

void IconMenu::toggleIconColor()
{
    String color = getAppProperties().getUserSettings()->getValue("icon");
    const String nextColor = color.equalsIgnoreCase("black") ? "white" : "black";
    getAppProperties().getUserSettings()->setValue("icon", nextColor);
    getAppProperties().saveIfNeeded();
    setIcon();
}

void IconMenu::loadActivePlugins()
{
    syncActivePlugins(true);
}

void IconMenu::syncActivePlugins(bool forceReload)
{
    if (forceReload)
    {
        PluginWindow::closeAllCurrentlyOpenWindows();
        player.setProcessor(nullptr);
        graph.clear();
        loadedPlugins.clear();
        graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
        graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);
    }
	const std::vector<PluginInstance> plugins = getOrderedActivePlugins();
	StringArray loadingErrors;
    for (const auto& loaded : loadedPlugins)
    {
        const bool retained = std::any_of(plugins.begin(), plugins.end(), [&](const PluginInstance& plugin)
        {
            return loaded.description.instanceId == plugin.instanceId && loaded.description.isDuplicateOf(plugin)
                && loaded.description.pluginFormatName == plugin.pluginFormatName;
        });
        if (!retained)
            if (const auto node = graph.getNodeForId(loaded.nodeId))
            {
                PluginWindow::closeWindowsFor(node);
                graph.removeNode(loaded.nodeId);
            }
    }
    std::vector<LoadedPlugin> nextLoaded;
    nextLoaded.reserve(plugins.size());
    for (int i = 1; i <= (int) plugins.size(); i++)
    {
        const PluginInstance& plugin = plugins[i - 1];
        const auto existing = std::find_if(loadedPlugins.begin(), loadedPlugins.end(), [&](const LoadedPlugin& loaded)
        {
            return loaded.description.instanceId == plugin.instanceId && loaded.description.isDuplicateOf(plugin)
                && loaded.description.pluginFormatName == plugin.pluginFormatName;
        });
        if (existing != loadedPlugins.end() && graph.getNodeForId(existing->nodeId) != nullptr)
        {
            nextLoaded.push_back({ plugin, existing->nodeId });
            continue;
        }
        nextLoaded.push_back({ plugin, 0 });
        String errorMessage;
        const double sampleRate = graph.getSampleRate() > 0.0 ? graph.getSampleRate() : 48000.0;
        const int blockSize = graph.getBlockSize() > 0 ? graph.getBlockSize() : 512;
        AudioPluginInstance* instance = formatManager.createPluginInstance(plugin, sampleRate, blockSize, errorMessage);
        if (instance == nullptr)
        {
            Logger::writeToLog("Could not load plugin " + plugin.name + ": " + errorMessage);
            loadingErrors.add(plugin.name + ": " + errorMessage);
            continue;
        }
		String pluginUid = getKey("state", plugin);
        String savedPluginState = getAppProperties().getUserSettings()->getValue(pluginUid);
        MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding(savedPluginState);
        const size_t savedPluginStateSize = savedPluginBinary.getSize();
        if (savedPluginStateSize > static_cast<size_t>(INT_MAX))
            Logger::writeToLog("Plugin state is too large to restore: " + plugin.name);
        else if (savedPluginStateSize > 0)
            instance->setStateInformation(savedPluginBinary.getData(),
                static_cast<int>(savedPluginStateSize));
        if (!PluginChain::supportsStereo(*instance))
        {
            loadingErrors.add(plugin.name + ": only plugins with stereo input and output are supported.");
            delete instance;
            continue;
        }
        uint32 nodeId = 1;
        while (nodeId == PluginChain::inputNodeId || nodeId == PluginChain::outputNodeId
               || graph.getNodeForId(nodeId) != nullptr)
            ++nodeId;
        graph.addNode(instance, nodeId);
        nextLoaded.back().nodeId = nodeId;
    }
    loadedPlugins.swap(nextLoaded);
    updatePluginConnections();
    if (forceReload)
        player.setProcessor(&graph);
    if (!loadingErrors.isEmpty())
    {
        player.audioDeviceError(loadingErrors.joinIntoString("\n"));
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Plugins skipped",
            loadingErrors.joinIntoString("\n") + "\nAudio continues through the remaining plugins.");
    }
    if (pluginListWindow != nullptr)
        pluginListWindow->refreshActivePlugins();
}

void IconMenu::restartAudio()
{
    if (restartingAudio)
        return;
    const ScopedValueSetter<bool> restarting(restartingAudio, true);
    deviceManager.closeAudioDevice();
    // Stop processing before asking plugins for state, then recreate their DSP state.
    savePluginStates();
    loadActivePlugins();
    deviceManager.restartLastAudioDevice();
    AudioIODevice* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr || !device->isOpen() || !device->isPlaying())
    {
        const String error = device != nullptr ? device->getLastError() : "No audio device is available.";
        player.audioDeviceError("Audio restart failed: " + error);
        AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Restart audio",
            "Could not restart audio. Check the selected device in Settings.\n" + error);
    }
}

void IconMenu::updatePluginConnections()
{
    PropertiesFile* settings = getAppProperties().getUserSettings();
    const StringArray issues = PluginChain::reconnect(graph, (int) loadedPlugins.size(),
        [&](int index) { return settings->getBoolValue(getKey("bypass", loadedPlugins[index].description)); },
        [&](int index) { return loadedPlugins[index].nodeId; });
    if (!issues.isEmpty())
    {
        player.audioDeviceError(issues.joinIntoString("\n"));
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Audio routing", issues.joinIntoString("\n"));
    }
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (settingsImported) return;
    if (changed == &knownPluginList)
    {
        ScopedPointer<XmlElement> savedPluginList (knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginList", savedPluginList);
        }
    }
    else if (changed == &activePluginList)
    {
        ScopedPointer<XmlElement> savedPluginList (activePluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginListActive", savedPluginList);
        }
        if (pluginListWindow != nullptr)
            pluginListWindow->refreshActivePlugins();
    }
}

#if JUCE_MAC
std::string IconMenu::exec(const char* cmd)
{
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while (!feof(pipe.get()))
    {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;
    }
    return result;
}
#endif

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    menu.addItem(menuOpenLightHost, "Open Light Host");
#if JUCE_WINDOWS
    bool runOnStartup = false;
    const bool startupAvailable = getRunOnStartupEnabled(runOnStartup);
    PopupMenu::Item startupItem;
    startupItem.itemID = menuRunOnStartup;
    startupItem.text = "Run on startup";
    startupItem.isEnabled = startupAvailable;
    startupItem.customComponent = new HostDesign::SwitchMenuItem(startupItem.text, runOnStartup);
    menu.addItem(startupItem);
#endif
    menu.addSeparator();
    menu.addColouredItem(menuQuit, "Quit", HostDesign::danger);
	#if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this)
		.withMinimumWidth(240).withStandardItemHeight(HostDesign::menuRowHeight),
		ModalCallbackFunction::forComponent(menuInvocationCallback, this));
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
	menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect)
		.withMinimumWidth(240).withStandardItemHeight(HostDesign::menuRowHeight),
		ModalCallbackFunction::forComponent(menuInvocationCallback, this));
	#endif
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    if (e.mods.isLeftButtonDown())
    {
        const uint32 now = Time::getMillisecondCounter();
        if (lastLeftClickTime != 0
            && now - lastLeftClickTime <= (uint32) MouseEvent::getDoubleClickTimeout())
        {
            lastLeftClickTime = 0;
            #if JUCE_MAC
            Process::setDockIconVisible(true);
            #endif
            Process::makeForegroundProcess();
            reloadPlugins();
        }
        else
            lastLeftClickTime = now;
        return;
    }

    if (!e.mods.isRightButtonDown())
        return;

    lastLeftClickTime = 0;
    #if JUCE_MAC
    Process::setDockIconVisible(true);
    #endif
    Process::makeForegroundProcess();
    startTimer(50);
}

void IconMenu::mouseDoubleClick(const MouseEvent& e)
{
    if (e.mods.isLeftButtonDown())
    {
        lastLeftClickTime = 0;
        #if JUCE_MAC
        Process::setDockIconVisible(true);
        #endif
        Process::makeForegroundProcess();
        reloadPlugins();
    }
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    if (id == 0)
    {
#if JUCE_MAC
        if (!PluginWindow::containsActiveWindows())
            Process::setDockIconVisible(false);
#endif
        return;
    }

    switch (id)
    {
    case menuOpenLightHost:
        im->reloadPlugins();
        break;
#if JUCE_WINDOWS
    case menuRunOnStartup:
        toggleRunOnStartup();
        if (im->pluginListWindow != nullptr) im->pluginListWindow->refreshStartupState();
        break;
#endif
    case menuQuit:
        JUCEApplication::getInstance()->quit();
        break;
    default:
        break;
    }
}

void IconMenu::addPluginToActiveList(const PluginDescription& plugin)
{
    activePluginList.addInstance(plugin, *getAppProperties().getUserSettings());
    syncActivePlugins(false);
}

void IconMenu::moveActivePlugin(int fromIndex, int toIndex)
{
    activePluginList.move(fromIndex, toIndex, *getAppProperties().getUserSettings());
    syncActivePlugins(false);
}

void IconMenu::performPluginAction(int id)
{
	if (id >= INDEX_DELETE && id < INDEX_DELETE + 1000000)
	{
		const int index = id - INDEX_DELETE;
		std::vector<PluginInstance> plugins = getOrderedActivePlugins();
		if (index < 0 || index >= (int) plugins.size())
			return;

		const PluginInstance plugin = plugins[index];
		activePluginList.removeInstance(plugin.instanceId, *getAppProperties().getUserSettings());
        syncActivePlugins(false);
	}
	else if (id >= INDEX_BYPASS && id < INDEX_BYPASS + 1000000)
	{
		const int index = id - INDEX_BYPASS;
		std::vector<PluginInstance> plugins = getOrderedActivePlugins();
		if (index < 0 || index >= (int) plugins.size())
			return;

		const String key = getKey("bypass", plugins[index]);
		PropertiesFile* settings = getAppProperties().getUserSettings();
		settings->setValue(key, !settings->getBoolValue(key));
		updatePluginConnections();
	}
	else if (id >= INDEX_EDIT && id < INDEX_EDIT + 1000000)
	{
        const int index = id - INDEX_EDIT;
        if (index < 0 || index >= (int) loadedPlugins.size())
            return;
		if (const AudioProcessorGraph::Node::Ptr node = graph.getNodeForId(loadedPlugins[index].nodeId))
			if (PluginWindow* const window = PluginWindow::getWindowFor(node, PluginWindow::Normal))
			{
                window->setName(loadedPlugins[index].description.displayName(*getAppProperties().getUserSettings()));
                window->setVisible(true);
				window->toFront(true);
			}
	}

	if (pluginListWindow != nullptr)
		pluginListWindow->refreshActivePlugins();
}

std::vector<PluginInstance> IconMenu::getOrderedActivePlugins()
{
    return activePluginList.ordered(*getAppProperties().getUserSettings());
}

String IconMenu::getKey(const String& type, const PluginDescription& plugin)
{
    return PluginState::key(type, plugin);
}

String IconMenu::getKey(const String& type, const PluginInstance& plugin)
{
    return plugin.settingsKey(type);
}

void IconMenu::deletePluginStates()
{
	std::vector<PluginInstance> list = getOrderedActivePlugins();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		String pluginUid = getKey("state", list[i]);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
    }
    getAppProperties().saveIfNeeded();
}

void IconMenu::savePluginStates(bool flushSettings)
{
	if (settingsImported)
		return;

    ScopedPointer<XmlElement> activeXml(activePluginList.createXml());
    getAppProperties().getUserSettings()->setValue("pluginListActive", activeXml);

    for (const auto& loaded : loadedPlugins)
    {
		AudioProcessorGraph::Node* node = graph.getNodeForId(loaded.nodeId);
		if (node == nullptr)
			continue;
        AudioProcessor& processor = *node->getProcessor();
		String pluginUid = getKey("state", loaded.description);
        MemoryBlock savedStateBinary;
        PluginState::capture(graph, processor, savedStateBinary);
        getAppProperties().getUserSettings()->setValue(pluginUid, savedStateBinary.toBase64Encoding());
    }
    if (flushSettings)
        getAppProperties().saveIfNeeded();
}

void IconMenu::exportBackup()
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	savePluginStates(false);

	ScopedPointer<XmlElement> audioState(deviceManager.createStateXml());
	settings->setValue("audioDeviceState", audioState);
	if (!settings->saveIfNeeded())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Could not save the current settings.");
		return;
	}

	FileChooser chooser("Export Light Host backup",
		File::getSpecialLocation(File::currentExecutableFile).getParentDirectory()
			.getChildFile("Light Host Backup.lightbak"),
		"*.lightbak");
	if (!chooser.browseForFileToSave(true))
		return;

	const File destination = chooser.getResult().withFileExtension(".lightbak");
	if (destination != chooser.getResult() && destination.exists()
		&& !AlertWindow::showOkCancelBox(AlertWindow::WarningIcon, "Export Backup",
			"The backup file already exists. Replace it?\n" + destination.getFullPathName(),
			"Replace", "Cancel"))
		return;
	if (destination == settings->getFile())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Choose a different file from the active settings file.");
		return;
	}

	const TemporaryFile temporaryBackup(destination);
    PluginBackup::ExportTask exportTask(temporaryBackup.getFile());
	XmlElement manifest("LIGHTHOST_BACKUP");
	manifest.setAttribute("version", 3);
	StringArray archivedPluginPaths;
	StringArray errors;
	int archivedPlugins = 0;

    const std::vector<PluginDescription> backupPlugins =
        PluginState::collectPlugins(knownPluginList, activePluginList);
	for (int i = 0; i < (int) backupPlugins.size(); ++i)
	{
		const PluginDescription* plugin = &backupPlugins[i];
		const File pluginFile(File::createFileWithoutCheckingPath(plugin->fileOrIdentifier));

		AudioPluginFormat* format = nullptr;
		for (int formatIndex = 0; formatIndex < formatManager.getNumFormats(); ++formatIndex)
		{
			AudioPluginFormat* candidate = formatManager.getFormat(formatIndex);
			if (candidate->canScanForPlugins()
				&& candidate->getName().equalsIgnoreCase(plugin->pluginFormatName))
			{
				format = candidate;
				break;
			}
		}

		if (format == nullptr)
			continue;
		if (!pluginFile.existsAsFile() && !pluginFile.isDirectory())
		{
			if (format->getDefaultLocationsToSearch().getNumPaths() > 0)
				errors.add(plugin->name + ": plugin file is missing at "
					+ pluginFile.getFullPathName() + ".");
			continue;
		}
		if (!format->fileMightContainThisPluginType(plugin->fileOrIdentifier)
			|| archivedPluginPaths.contains(pluginFile.getFullPathName()))
			continue;

		const String archiveRoot = "plugins/" + String(archivedPlugins);
        exportTask.plugins.add(pluginFile);

		XmlElement* pluginEntry = manifest.createNewChildElement("PLUGIN");
		pluginEntry->setAttribute("id", archivedPlugins);
		pluginEntry->setAttribute("format", format->getName());
		pluginEntry->setAttribute("source", pluginFile.getFullPathName());
		pluginEntry->setAttribute("archiveRoot", archiveRoot);
        pluginEntry->setAttribute("isDirectory", pluginFile.isDirectory());
		archivedPluginPaths.add(pluginFile.getFullPathName());
		++archivedPlugins;
	}

	if (!errors.isEmpty())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Could not create a complete backup because some plugin files are unavailable.\n\n"
			+ errors.joinIntoString("\n"));
		return;
	}

	ScopedPointer<XmlElement> settingsXml(settings->createXml("PROPERTIES"));
    exportTask.settingsText = settingsXml->createDocument(String(), true);
    exportTask.manifestText = manifest.createDocument(String(), true);
    if (!exportTask.runThread(3))
        return;
	if (!exportTask.succeeded || !temporaryBackup.overwriteTargetFileWithTemporary())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
            exportTask.error.isNotEmpty() ? exportTask.error : "Could not write the backup archive.");
		return;
	}

	AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Export Backup",
		"Backup exported with settings and " + String(archivedPlugins) + " plugin file(s). "
		"Plugin installers, licenses, and external dependencies are not included.");
}

bool IconMenu::importBackup()
{
	FileChooser chooser("Import Light Host backup",
		File::getSpecialLocation(File::currentExecutableFile).getParentDirectory(), "*.lightbak");
	if (!chooser.browseForFileToOpen())
		return false;
	if (!chooser.getResult().hasFileExtension(".lightbak"))
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"Please select a .lightbak backup file.");
		return false;
	}

	ZipFile archive(chooser.getResult());
	const int manifestIndex = archive.getIndexOfFileName("manifest.xml");
	const int settingsIndex = archive.getIndexOfFileName("settings.xml");
	if (manifestIndex < 0 || settingsIndex < 0)
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"The selected file is not a Light Host backup.");
		return false;
	}

	ScopedPointer<InputStream> manifestStream(archive.createStreamForEntry(manifestIndex));
	ScopedPointer<InputStream> settingsStream(archive.createStreamForEntry(settingsIndex));
	if (manifestStream == nullptr || settingsStream == nullptr)
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"Could not read the backup archive.");
		return false;
	}

	ScopedPointer<XmlElement> manifest(XmlDocument::parse(manifestStream->readEntireStreamAsString()));
	ScopedPointer<XmlElement> settingsXml(XmlDocument::parse(settingsStream->readEntireStreamAsString()));
	if (manifest == nullptr || !manifest->hasTagName("LIGHTHOST_BACKUP")
		|| !PluginBackup::supportsVersion(manifest->getIntAttribute("version"))
		|| settingsXml == nullptr || !settingsXml->hasTagName("PROPERTIES"))
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"The backup manifest or settings file is invalid or unsupported.");
		return false;
	}

	if (!AlertWindow::showOkCancelBox(AlertWindow::QuestionIcon, "Import Backup",
		"This will install included plugin files and replace your current Light Host settings. "
		"Existing plugin files will not be overwritten. Continue?",
		"Import", "Cancel"))
		return false;

	PropertySet importedSettings;
	importedSettings.restoreFromXml(*settingsXml);
	if (!PluginBackup::hasValidPluginLists(importedSettings, manifest->getIntAttribute("version") >= 3))
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"The backup contains invalid plugin settings. Your settings were not changed.");
		return false;
	}
	PropertiesFile* currentSettings = getAppProperties().getUserSettings();
	StringArray oldPluginPaths, newPluginPaths;
	StringArray localScanFormats, localScanPaths;
	StringArray errors;

	forEachXmlChildElementWithTagName(*manifest, pluginEntry, "PLUGIN")
	{
		const String id = pluginEntry->getStringAttribute("id");
		const String archiveRoot = pluginEntry->getStringAttribute("archiveRoot");
		const String sourcePath = pluginEntry->getStringAttribute("source");
		const String formatName = pluginEntry->getStringAttribute("format");
		if (id.isEmpty() || archiveRoot != "plugins/" + id || sourcePath.isEmpty())
		{
			errors.add("The backup contains an invalid plugin entry.");
			continue;
		}
        if (manifest->getIntAttribute("version") >= 2 && !pluginEntry->hasAttribute("isDirectory"))
        {
            errors.add("The backup is missing the plugin file layout.");
            continue;
        }

		AudioPluginFormat* format = nullptr;
		for (int formatIndex = 0; formatIndex < formatManager.getNumFormats(); ++formatIndex)
		{
			AudioPluginFormat* candidate = formatManager.getFormat(formatIndex);
			if (candidate->canScanForPlugins()
				&& candidate->getName().equalsIgnoreCase(formatName))
			{
				format = candidate;
				break;
			}
		}
		if (format == nullptr)
		{
			errors.add(formatName + ": this plugin format is not supported on this computer.");
			continue;
		}

		const File sourcePlugin(File::createFileWithoutCheckingPath(sourcePath));
		const String pluginFileName = sourcePlugin.getFileName();
		FileSearchPath scanPaths =
			PluginListComponent::getLastSearchPath(*currentSettings, *format);
		for (int i = 0; i < localScanFormats.size(); ++i)
			if (localScanFormats[i].equalsIgnoreCase(formatName))
				scanPaths = FileSearchPath(localScanPaths[i]);
		if (pluginFileName.isEmpty()
			|| pluginFileName == "." || pluginFileName == ".."
			|| pluginFileName.containsChar('/') || pluginFileName.containsChar('\\'))
		{
			errors.add(formatName + ": no valid plugin scan location or filename is available.");
			continue;
		}

		const File restoreRoot = File::getSpecialLocation(File::currentExecutableFile)
			.getParentDirectory().getChildFile("VST Plugins");
		const File destinationPlugin = PluginBackup::restoreDestination(sourcePlugin, scanPaths, restoreRoot);
		if (!destinationPlugin.exists() && !destinationPlugin.isAChildOf(restoreRoot))
		{
			errors.add(formatName + ": the plugin destination is outside its scan location.");
			continue;
		}
		scanPaths.add(destinationPlugin.getParentDirectory());
		const String archivePrefix = archiveRoot + "/";
		int archivedFileCount = 0;
		bool unsafeArchivePath = false;
		for (int entryIndex = 0; entryIndex < archive.getNumEntries(); ++entryIndex)
		{
			const ZipFile::ZipEntry* entry = archive.getEntry(entryIndex);
			if (entry != nullptr && entry->filename.startsWith(archivePrefix))
			{
				++archivedFileCount;
				if (!PluginBackup::isSafePath(entry->filename.substring(archivePrefix.length())))
					unsafeArchivePath = true;
			}
		}
		if (unsafeArchivePath)
		{
			errors.add(sourcePlugin.getFileName() + ": backup contains an unsafe plugin path.");
			continue;
		}
		if (archivedFileCount == 0)
		{
			errors.add(sourcePlugin.getFileName() + ": plugin files are missing from the backup.");
			continue;
		}
        const bool directory = PluginBackup::isDirectoryArchive(*pluginEntry, archive,
            archivePrefix, pluginFileName);
        if (!directory && (archivedFileCount != 1
            || archive.getIndexOfFileName(archivePrefix + pluginFileName) < 0))
        {
            errors.add(pluginFileName + ": backup has an invalid single-file plugin layout.");
            continue;
        }

		int localFormatIndex = -1;
		for (int i = 0; i < localScanFormats.size(); ++i)
			if (localScanFormats[i].equalsIgnoreCase(format->getName()))
				localFormatIndex = i;
		if (localFormatIndex >= 0)
			localScanPaths.set(localFormatIndex, scanPaths.toString());
		else
		{
			localScanFormats.add(format->getName());
			localScanPaths.add(scanPaths.toString());
		}

		bool hasPluginFiles = false;
		bool pluginImportFailed = false;

		if (destinationPlugin.exists())
		{
			if (format->fileMightContainThisPluginType(destinationPlugin.getFullPathName()))
			{
				oldPluginPaths.add(sourcePath);
				newPluginPaths.add(destinationPlugin.getFullPathName());
				continue;
			}
			errors.add(destinationPlugin.getFullPathName() + ": destination already exists.");
			continue;
		}

		for (int entryIndex = 0; entryIndex < archive.getNumEntries(); ++entryIndex)
		{
			const ZipFile::ZipEntry* entry = archive.getEntry(entryIndex);
			if (entry == nullptr || !entry->filename.startsWith(archivePrefix))
				continue;

			const String relativePath = entry->filename.substring(archivePrefix.length());
			if (!PluginBackup::isSafePath(relativePath))
			{
				errors.add("The backup contains an unsafe plugin path.");
				pluginImportFailed = true;
				break;
			}

			const File target = PluginBackup::extractionTarget(destinationPlugin, relativePath, directory);
			if (target == File() || target.exists()
				|| !target.getParentDirectory().createDirectory())
			{
				errors.add(target.getFullPathName() + ": could not create plugin file.");
				pluginImportFailed = true;
				break;
			}

			ScopedPointer<InputStream> input(archive.createStreamForEntry(entryIndex));
			FileOutputStream output(target);
			if (input == nullptr || output.failedToOpen())
			{
				errors.add(target.getFullPathName() + ": could not extract plugin file.");
				pluginImportFailed = true;
				break;
			}
			const int64 bytesWritten = output.writeFromInputStream(*input, -1);
			output.flush();
			if (bytesWritten != entry->uncompressedSize || output.getStatus().failed())
			{
				errors.add(target.getFullPathName() + ": could not extract plugin file.");
				pluginImportFailed = true;
				break;
			}
			hasPluginFiles = true;
		}

		if (!pluginImportFailed && hasPluginFiles
			&& format->fileMightContainThisPluginType(destinationPlugin.getFullPathName()))
		{
			oldPluginPaths.add(sourcePath);
			newPluginPaths.add(destinationPlugin.getFullPathName());
		}
		else if (!pluginImportFailed)
			errors.add(sourcePlugin.getFileName() + ": plugin files are missing or invalid.");
	}

	if (!errors.isEmpty())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"Settings were not changed because some plugin files could not be restored. "
			"Files copied before the error may remain in the plugin scan folders.\n\n"
			+ errors.joinIntoString("\n"));
		return false;
	}

	PluginBackup::prepareImportedSettings(importedSettings, oldPluginPaths, newPluginPaths,
		File::getSpecialLocation(File::currentExecutableFile).getParentDirectory());
	for (int i = 0; i < localScanFormats.size(); ++i)
		importedSettings.setValue("lastPluginScanPath_" + localScanFormats[i], localScanPaths[i]);

	PropertySet previousSettings;
	previousSettings.addAllPropertiesFrom(*currentSettings);
	currentSettings->clear();
	currentSettings->addAllPropertiesFrom(importedSettings);
	if (!currentSettings->saveIfNeeded())
	{
		currentSettings->clear();
		currentSettings->addAllPropertiesFrom(previousSettings);
		const bool restored = currentSettings->saveIfNeeded();
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			restored ? "Could not save the imported settings. Your previous settings were restored."
				: "Could not save the imported settings or restore the previous settings.");
		return false;
	}

	settingsImported = true;
	AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Import Backup",
		"Backup imported. Light Host will now restart to apply the restored settings.");
	return true;
}

void IconMenu::reloadPlugins()
{
	if (pluginListWindow == nullptr)
		pluginListWindow = new PluginListWindow(*this);
	pluginListWindow->showPluginsView();
	pluginListWindow->toFront(true);
}
