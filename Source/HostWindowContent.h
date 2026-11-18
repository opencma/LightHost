#ifndef LIGHTHOST_HOST_WINDOW_CONTENT_H
#define LIGHTHOST_HOST_WINDOW_CONTENT_H

class LightHostMainWindowContent : public Component, private Button::Listener, private Timer
{
public:
    enum ViewType { pluginsView, settingsView, aboutView };

    explicit LightHostMainWindowContent (IconMenu& owner_) : owner (owner_)
    {
        setOpaque (true);
        for (auto* button : { &pluginsButton, &settingsButton, &aboutButton })
        {
            addAndMakeVisible (button);
            button->addListener (this);
        }
        addButton.setButtonText ("+  Add plugin");
        addButton.addListener (this);
        addAndMakeVisible (addButton);
        readyStatus.setFont (Font (14.0f, Font::bold));
        readyDetails.setFont (HostDesign::bodyFont(15.0f));
        readyDetails.setColour (Label::textColourId, HostDesign::secondary);
        addAndMakeVisible (readyStatus);
        addAndMakeVisible (readyDetails);
        pluginsPanel = new IconMenu::PluginManagementComponent (owner);
        aboutPanel = new LightHostAboutView();
        addAndMakeVisible (pluginsPanel);
        addChildComponent (aboutPanel);
        setView (pluginsView);
        timerCallback();
    }

    void setStatusUpdatesEnabled (bool enabled)
    {
        if (enabled)
        {
            timerCallback();
            startTimer (750);
        }
        else
            stopTimer();
    }

    void setView (ViewType view)
    {
        currentView = view;
        pluginsButton.setToggleState (view == pluginsView, dontSendNotification);
        settingsButton.setToggleState (view == settingsView, dontSendNotification);
        aboutButton.setToggleState (view == aboutView, dontSendNotification);
        pluginsPanel->setVisible (view == pluginsView);
        addButton.setVisible (view == pluginsView);
        if (view == settingsView && settingsPanel == nullptr)
        {
            settingsPanel = new LightHostSettingsView (owner);
            addChildComponent (settingsPanel);
            resized();
        }
        if (settingsPanel != nullptr)
        {
            settingsPanel->setVisible (view == settingsView);
            if (view == settingsView) settingsPanel->refreshStartupState();
        }
        aboutPanel->setVisible (view == aboutView);
        repaint();
    }

    void refreshActivePlugins() { pluginsPanel->refreshActivePlugins(); }
    void refreshStartupState() { if (settingsPanel != nullptr) settingsPanel->refreshStartupState(); }

    void lookAndFeelChanged() override
    {
        readyDetails.setColour(Label::textColourId, HostDesign::secondary);
        timerCallback();
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'F')
        {
            setView (pluginsView);
            pluginsPanel->focusLibrary();
            return true;
        }
        return false;
    }

    void paint (Graphics& g) override
    {
        g.fillAll (HostDesign::canvas);
        g.setColour (HostDesign::sidebar);
        g.fillRect (0, 0, sidebarWidth, getHeight() - footerHeight);
        g.setColour (HostDesign::border);
        g.drawVerticalLine (sidebarWidth, 0, (float) getHeight() - footerHeight);
        g.drawHorizontalLine (headerHeight, (float) sidebarWidth, (float) getWidth());
        g.drawHorizontalLine (getHeight() - footerHeight, 0, (float) getWidth());

        HostDesign::drawSymbol (g, HostDesign::waveform, juce::Rectangle<float> (22, 28, 25, 25), HostDesign::text);
        g.setColour (HostDesign::text);
        g.setFont (Font (21.0f, Font::bold));
        g.drawText ("Light Host", 58, 24, 120, 34, Justification::centredLeft);
        const String heading = currentView == pluginsView ? "Signal chain"
                             : currentView == settingsView ? "Settings" : "About";
        const String subtitle = currentView == pluginsView ? "Plugins process audio from top to bottom."
            : currentView == settingsView ? "Your audio setup and app preferences."
            : "A little host for your favorite effects.";
        g.setFont (Font (28.0f, Font::bold));
        g.drawText (heading, sidebarWidth + 28, 17, getWidth() - sidebarWidth - 190, 34, Justification::centredLeft);
        g.setFont (HostDesign::bodyFont(15.0f));
        g.setColour (HostDesign::secondary);
        g.drawText (subtitle, sidebarWidth + 28, 55, getWidth() - sidebarWidth - 56, 24, Justification::centredLeft);

        g.setColour (statusColour);
        g.fillEllipse (22.0f, (float) getHeight() - 91, 8, 8);
        g.setFont (HostDesign::bodyFont(15.0f));
        g.setColour (HostDesign::secondary);
        g.drawText ("Settings save automatically", 20, getHeight() - footerHeight, 240, footerHeight, Justification::centredLeft);
#if JUCE_WINDOWS
        g.drawText ("Close the window to keep audio running in the system tray", 280, getHeight() - footerHeight,
                    getWidth() - 300, footerHeight, Justification::centredRight);
#else
        g.drawText ("Close the window to keep audio running", 280, getHeight() - footerHeight,
                    getWidth() - 300, footerHeight, Justification::centredRight);
#endif
    }

    void resized() override
    {
        pluginsButton.setBounds (12, 100, sidebarWidth - 24, 42);
        settingsButton.setBounds (12, 148, sidebarWidth - 24, 42);
        aboutButton.setBounds (12, 196, sidebarWidth - 24, 42);
        readyStatus.setBounds (35, getHeight() - 100, sidebarWidth - 44, 24);
        readyDetails.setBounds (35, getHeight() - 75, sidebarWidth - 44, 20);
        addButton.setBounds (getWidth() - 152, 25, 124, 34);
        const juce::Rectangle<int> content (sidebarWidth + 28, headerHeight + 24,
            jmax (0, getWidth() - sidebarWidth - 56), jmax (0, getHeight() - headerHeight - footerHeight - 48));
        pluginsPanel->setBounds (content);
        if (settingsPanel != nullptr) settingsPanel->setBounds (content);
        aboutPanel->setBounds (content);
    }

private:
    void buttonClicked (Button* button) override
    {
        if (button == &pluginsButton) setView (pluginsView);
        else if (button == &settingsButton) setView (settingsView);
        else if (button == &aboutButton) setView (aboutView);
        else if (button == &addButton) pluginsPanel->importPlugins();
    }

    void timerCallback() override
    {
        auto* device = owner.getDeviceManager().getCurrentAudioDevice();
        const bool running = device != nullptr && device->isOpen() && device->isPlaying();
        const String status = running ? "Audio running" : device != nullptr ? "Audio stopped" : "No audio device";
        const String details = running ? String (device->getCurrentSampleRate() / 1000.0, 1)
            + " kHz / " + String (device->getCurrentBufferSizeSamples())
            : "Check audio settings";
        const Colour colour = running ? HostDesign::green : HostDesign::warning;
        if (status != readyStatus.getText() || colour != statusColour)
        {
            readyStatus.setText (status, dontSendNotification);
            statusColour = colour;
            repaint (0, getHeight() - 106, sidebarWidth, 70);
        }
        readyDetails.setText (details, dontSendNotification);
    }

    enum { sidebarWidth = 184, headerHeight = 98, footerHeight = 32 };
    IconMenu& owner;
    ViewType currentView = pluginsView;
    HostDesign::NavigationButton pluginsButton { "Signal chain", HostDesign::chain };
    HostDesign::NavigationButton settingsButton { "Settings", HostDesign::settings };
    HostDesign::NavigationButton aboutButton { "About", HostDesign::info };
    TextButton addButton;
    Label readyStatus, readyDetails;
    Colour statusColour;
    ScopedPointer<IconMenu::PluginManagementComponent> pluginsPanel;
    ScopedPointer<LightHostSettingsView> settingsPanel;
    ScopedPointer<LightHostAboutView> aboutPanel;
};
#endif
