#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_cryptography/juce_cryptography.h>
#include "../Source/UpdateManifestValidation.hpp"
#include "../Source/LinuxUpdatePackage.hpp"
#include <iostream>
#include <unistd.h>

namespace
{
using namespace juce;

Result fetch (const String& url, size_t limit, MemoryBlock& data)
{
    int status = 0;
    auto stream = URL (url).createInputStream (
        URL::InputStreamOptions (URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (15000).withStatusCode (&status));
    if (stream == nullptr || status != 200)
        return Result::fail ("Download failed (HTTP " + String (status) + "): " + url);
    char buffer[65536];
    for (;;)
    {
        const auto count = stream->read (buffer, sizeof buffer);
        if (count <= 0)
        {
            if (! stream->isExhausted())
                return Result::fail ("The download was interrupted.");
            break;
        }
        if (data.getSize() + static_cast<size_t> (count) > limit)
            return Result::fail ("The download exceeds its permitted size.");
        data.append (buffer, static_cast<size_t> (count));
    }
    return data.isEmpty() ? Result::fail ("The download was empty.") : Result::ok();
}

Result downloadPackage (MemoryBlock& data)
{
    MemoryBlock response;
    auto result = fetch ("https://annaspannertechsolutions.uk/lighthost/latest", 65536, response);
    if (result.failed())
        return result;
    const auto manifest = JSON::parse (String::fromUTF8 (
        static_cast<const char*> (response.getData()), static_cast<int> (response.getSize())));
#if defined(__aarch64__)
    const String platform = "linux-arm64-package";
#else
    const String platform = "linux-x64-package";
#endif
    String url, hash;
    if (! getLightHostReleaseAsset (manifest, platform, url, hash))
        return Result::fail ("The update manifest has no valid application package for " + platform + ".");
    result = fetch (url, 512u * 1024u * 1024u, data);
    if (result.failed())
        return result;
    if (! SHA256 (data).toHexString().equalsIgnoreCase (hash))
        return Result::fail ("The downloaded package failed SHA-256 verification.");
    return Result::ok();
}

String desktopEscape (String text)
{
    return text.replace ("\\", "\\\\").replace ("\"", "\\\"")
               .replace ("`", "\\`").replace ("$", "\\$").replace ("%", "%%");
}

Result createShortcut (const File& directory, const File& application)
{
    if (! directory.createDirectory())
        return Result::fail ("Could not create shortcut directory: " + directory.getFullPathName());
    const auto shortcut = directory.getChildFile ("lighthost.desktop");
    const auto content = "[Desktop Entry]\nType=Application\nName=Light Host\nExec=\""
                       + desktopEscape (application.getFullPathName())
                       + "\"\nTerminal=false\nCategories=AudioVideo;Audio;\n";
    if (! shortcut.replaceWithText (content) || ! shortcut.setExecutePermission (true))
        return Result::fail ("Light Host was installed, but could not create " + shortcut.getFullPathName());
    return Result::ok();
}

Result install (bool menuShortcut, bool desktopShortcut)
{
    MemoryBlock package;
    auto result = downloadPackage (package);
    if (result.failed())
        return result;
    const auto home = File::getSpecialLocation (File::userHomeDirectory);
    const auto directory = home.getChildFile (".local/share/Light Host");
    result = installLinuxPackage (package, directory);
    if (result.failed())
        return result;

    const auto application = directory.getChildFile ("Light Host");
    if (menuShortcut)
    {
        result = createShortcut (home.getChildFile (".local/share/applications"), application);
        if (result.failed())
            return result;
    }
    if (desktopShortcut)
        return createShortcut (File::getSpecialLocation (File::userDesktopDirectory), application);
    return Result::ok();
}

class InstallTask final : public ThreadWithProgressWindow
{
public:
    InstallTask (bool menuShortcut, bool desktopShortcut)
        : ThreadWithProgressWindow ("Light Host Setup", false, false),
          menu (menuShortcut), desktop (desktopShortcut)
    {
        setStatusMessage ("Downloading and installing Light Host. Please wait...");
    }

    void run() override { result = install (menu, desktop); }

    Result result = Result::fail ("Installation did not complete.");

private:
    bool menu, desktop;
};

class InstallerApplication final : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Light Host Setup"; }
    const String getApplicationVersion() override { return "1.2.2"; }
    void initialise (const String&) override
    {
        const bool terminal = isatty (STDIN_FILENO) && isatty (STDOUT_FILENO);
        bool menu = false, desktop = false;
        if (terminal)
        {
            std::string answer;
            std::cout << "Download and install Light Host for this user? [y/N] " << std::flush;
            std::getline (std::cin, answer);
            if (answer != "y" && answer != "Y") { quit(); return; }
            std::cout << "Create an application menu shortcut? [y/N] " << std::flush;
            std::getline (std::cin, answer); menu = answer == "y" || answer == "Y";
            std::cout << "Create a desktop shortcut? [y/N] " << std::flush;
            std::getline (std::cin, answer); desktop = answer == "y" || answer == "Y";
        }
        else
        {
            if (! AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Light Host Setup",
                "Download and install Light Host for this user? Close Light Host first."))
            { quit(); return; }
            menu = AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Light Host Setup",
                                                 "Create an application menu shortcut?", "Yes", "No");
            desktop = AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Light Host Setup",
                                                    "Create a desktop shortcut?", "Yes", "No");
        }
        auto result = Result::fail ("Installation did not complete.");
        if (terminal)
            result = install (menu, desktop);
        else
        {
            InstallTask task (menu, desktop);
            if (task.runThread())
                result = task.result;
        }
        const auto message = result.wasOk() ? String ("Light Host was installed successfully.")
                                            : result.getErrorMessage();
        if (terminal)
            (result.wasOk() ? std::cout : std::cerr) << message << std::endl;
        else
            AlertWindow::showMessageBox (result.wasOk() ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon,
                                         "Light Host Setup", message);
        setApplicationReturnValue (result.wasOk() ? 0 : 1);
        quit();
    }
    void shutdown() override {}
};
}

START_JUCE_APPLICATION (InstallerApplication)
