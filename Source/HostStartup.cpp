#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include "HostLookAndFeel.h"

#if ! (JUCE_PLUGINHOST_VST || JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the audio plugin host, you probably want to enable VST and/or AU support"
#endif

class PluginHostApp  : public JUCEApplication
{

public:
    PluginHostApp() {}

    void initialise (const String&) override
    {
        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";
        // Batch routine UI changes; explicit backup and shutdown paths flush.
        options.millisecondsBeforeSaving = 3000;

        checkArguments(&options);

        const File appDirectory = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();
        options.folderName = appDirectory.getFullPathName();
        const File oldPortableSettings = options.getDefaultFile();
        const File dataDirectory = appDirectory.getChildFile("Data");
        options.folderName = dataDirectory.getFullPathName();
        const File portableSettings = options.getDefaultFile();
        if (!dataDirectory.createDirectory().wasOk())
        {
            AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Settings",
                "Could not create the Data folder. Check that the application folder is writable.");
            quit();
            return;
        }
        if (!portableSettings.exists() && oldPortableSettings.existsAsFile())
        {
            const bool migrated = oldPortableSettings.moveFileTo(portableSettings);
            if (!migrated)
            {
                AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Settings",
                    "Could not migrate settings into the Data folder. Your existing settings were preserved.");
                quit();
                return;
            }
        }
        const File oldLog = oldPortableSettings.withFileExtension("audio.log");
        const File newLog = portableSettings.withFileExtension("audio.log");
        if (oldLog.existsAsFile() && !newLog.exists())
            oldLog.moveFileTo(newLog);

        appProperties = new ApplicationProperties();
        appProperties->setStorageParameters (options);

        HostDesign::setDarkMode(appProperties->getUserSettings()->getBoolValue("darkMode", true));
        lookAndFeel.refreshPalette();
        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        mainWindow = new IconMenu();
		#if JUCE_MAC
			Process::setDockIconVisible(false);
		#endif
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    void restartAfterImport()
    {
        // Recreate the host only after the import button callback has returned.
        // The old host suppresses state writes after a successful import.
        mainWindow = nullptr;
        appProperties = nullptr;
        initialise(String());
    }

    const String getApplicationName() override       { return "Light Host"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       {
        StringArray multiInstance = getParameter("-multi-instance");
        return multiInstance.size() == 2;
    }

    ScopedPointer<ApplicationProperties> appProperties;
    LightHostLookAndFeel lookAndFeel;

private:
    ScopedPointer<IconMenu> mainWindow;

    StringArray getParameter(String lookFor) {
        StringArray parameters = getCommandLineParameterArray();
        StringArray found;
        for (int i = 0; i < parameters.size(); ++i)
        {
            String param = parameters[i];
            if (param.contains(lookFor))
            {
                found.add(lookFor);
                int delimiter = param.indexOf(0, "=") + 1;
                String val = param.substring(delimiter);
                found.add(val);
                return found;
            }
        }
        return found;
    }

    void checkArguments(PropertiesFile::Options *options) {
        StringArray multiInstance = getParameter("-multi-instance");
        if (multiInstance.size() == 2)
            options->filenameSuffix = multiInstance[1] + "." + options->filenameSuffix;
    }
};

static PluginHostApp& getApp()                      { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationProperties& getAppProperties()           { return *getApp().appProperties; }
void restartAfterSettingsImport()
{
    MessageManager::callAsync([] { getApp().restartAfterImport(); });
}

START_JUCE_APPLICATION (PluginHostApp)
