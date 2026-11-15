#include <JuceHeader.h>
#include "ErrorReporting.hpp"
#include "IconMenu.hpp"

#if ! JUCE_PLUGINHOST_VST
 #error "Light Host requires VST2 hosting support"
#endif

#if ! (JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the audio plugin host, you probably want to enable VST3 and/or AU support"
#endif

class HostLookAndFeel : public LookAndFeel_V4
{
public:
    std::unique_ptr<AlertWindow> createAlertWindowForOptions (const MessageBoxOptions& options) override
    {
        if (options.getTitle() == TRANS ("Error when trying to open audio device!"))
            return LookAndFeel_V4::createAlertWindowForOptions (
                options.withMessage (options.getMessage() + audioDeviceHelp()));
        return LookAndFeel_V4::createAlertWindowForOptions (options);
    }
};

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

        checkArguments(&options);

        appProperties.reset (new ApplicationProperties());
        appProperties->setStorageParameters (options);

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        mainWindow.reset (new IconMenu());
		#if JUCE_MAC
			Process::setDockIconVisible(false);
		#endif
    }

    void shutdown() override
    {
        mainWindow.reset();
        appProperties.reset();
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    void unhandledException (const std::exception* exception, const String& sourceFilename,
                             int lineNumber) override
    {
        const String reason = exception != nullptr ? String::fromUTF8 (exception->what())
                                                   : "Unknown exception";
        showErrorWithDiagnostics ("Unhandled application exception",
                                  reason + "\nCaught at: " + sourceFilename + ":" + String (lineNumber));
    }

    const String getApplicationName() override       { return "Light Host"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       {
        StringArray multiInstance = getParameter("-multi-instance");
        return multiInstance.size() == 2;
    }

    ApplicationCommandManager commandManager;
    std::unique_ptr<ApplicationProperties> appProperties;
    HostLookAndFeel lookAndFeel;

private:
    std::unique_ptr<IconMenu> mainWindow;

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
ApplicationCommandManager& getCommandManager()      { return getApp().commandManager; }
ApplicationProperties& getAppProperties()           { return *getApp().appProperties; }

START_JUCE_APPLICATION (PluginHostApp)
