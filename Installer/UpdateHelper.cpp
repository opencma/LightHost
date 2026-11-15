#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace fs = std::filesystem;

namespace
{
void writeStatus (const fs::path& path, const std::string& message)
{
    std::error_code error;
    fs::create_directories (path.parent_path(), error);
    std::ofstream output (path, std::ios::trunc);
    if (output)
        output << message;
}

bool waitForProcess (pid_t pid)
{
    if (pid <= 0)
        return false;
    while (::kill (pid, 0) == 0 || errno == EPERM)
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
    return errno == ESRCH;
}

bool launchDetached (const std::vector<std::string>& arguments)
{
    std::vector<char*> argv;
    argv.reserve (arguments.size() + 1);
    for (const auto& argument : arguments)
        argv.push_back (const_cast<char*> (argument.c_str()));
    argv.push_back (nullptr);
    pid_t child = 0;
    return posix_spawn (&child, argv[0], nullptr, nullptr, argv.data(), environ) == 0;
}

bool runAndWait (const std::vector<std::string>& arguments)
{
    std::vector<char*> argv;
    argv.reserve (arguments.size() + 1);
    for (const auto& argument : arguments)
        argv.push_back (const_cast<char*> (argument.c_str()));
    argv.push_back (nullptr);
    pid_t child = 0;
    if (posix_spawn (&child, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
        return false;
    int status = 0;
    while (::waitpid (child, &status, 0) < 0)
        if (errno != EINTR)
            return false;
    return WIFEXITED (status) && WEXITSTATUS (status) == 0;
}

int installLinux (pid_t pid, const fs::path& source, const fs::path& target, const fs::path& status,
                  const fs::path& replacementHelper)
{
    if (! waitForProcess (pid))
    {
        writeStatus (status, "The updater could not wait for Light Host to close.");
        return 1;
    }

    if (::chmod (source.c_str(), 0755) != 0
        || (! replacementHelper.empty() && ::chmod (replacementHelper.c_str(), 0755) != 0))
    {
        writeStatus (status, "The downloaded Linux app could not be made executable.");
        return 1;
    }

    std::error_code error;
    if (! replacementHelper.empty())
    {
        const auto helper = target.parent_path() / "LightHostUpdater";
        const auto oldApp = source.parent_path() / "previous-app";
        const auto oldHelper = source.parent_path() / "previous-updater";
        fs::rename (target, oldApp, error);
        if (error)
        {
            writeStatus (status, "Could not back up the Linux app: " + error.message());
            return 1;
        }
        fs::rename (helper, oldHelper, error);
        if (error)
        {
            std::error_code restoreError;
            fs::rename (oldApp, target, restoreError);
            writeStatus (status, "Could not back up the updater: " + error.message()
                                 + (restoreError ? ". Previous app remains at " + oldApp.string() : ""));
            return 1;
        }
        fs::rename (source, target, error);
        if (! error)
            fs::rename (replacementHelper, helper, error);
        if (error)
        {
            std::error_code removeError, appRestoreError, helperRestoreError;
            if (fs::exists (target))
                fs::remove (target, removeError);
            fs::rename (oldApp, target, appRestoreError);
            fs::rename (oldHelper, helper, helperRestoreError);
            writeStatus (status, "Could not install the Linux update: " + error.message()
                                 + ((removeError || appRestoreError || helperRestoreError)
                                    ? ". Some backups remain in " + source.parent_path().string() : ""));
            return 1;
        }
        fs::remove (oldApp, error);
        if (error)
        {
            writeStatus (status, "Update installed, but could not remove old app: " + error.message());
            return 1;
        }
        fs::remove (oldHelper, error);
        if (error)
        {
            writeStatus (status, "Update installed, but could not remove old updater: " + error.message());
            return 1;
        }
        fs::remove (source.parent_path(), error);
        if (error)
        {
            writeStatus (status, "Update installed, but could not remove staging directory: " + error.message());
            return 1;
        }
    }
    else
        fs::rename (source, target, error);
    if (error)
    {
        writeStatus (status, "Could not replace the Linux app: " + error.message());
        return 1;
    }
    writeStatus (status, "Light Host was updated successfully.");
    if (! launchDetached ({ target.string() }))
    {
        writeStatus (status, "The update was installed, but Light Host could not be restarted.");
        return 1;
    }
    return 0;
}

#if defined(__APPLE__)
bool runDitto (const fs::path& archive, const fs::path& destination)
{
    std::vector<std::string> arguments { "/usr/bin/ditto", "-x", "-k",
                                         archive.string(), destination.string() };
    return runAndWait (arguments);
}

int installMac (pid_t pid, const fs::path& archive, const fs::path& target, const fs::path& status)
{
    if (! waitForProcess (pid))
    {
        writeStatus (status, "The updater could not wait for Light Host to close.");
        return 1;
    }

    std::string temporaryTemplate = (target.parent_path() / ".LightHostUpdate-XXXXXX").string();
    std::vector<char> temporaryPath (temporaryTemplate.begin(), temporaryTemplate.end());
    temporaryPath.push_back ('\0');
    if (::mkdtemp (temporaryPath.data()) == nullptr)
    {
        writeStatus (status, "Could not create a temporary directory for the macOS update.");
        return 1;
    }
    const fs::path staging (temporaryPath.data());
    if (! runDitto (archive, staging))
    {
        writeStatus (status, "Could not extract the macOS update archive.");
        fs::remove_all (staging);
        return 1;
    }

    const fs::path replacement = staging / "Light Host.app";
    if (! fs::is_directory (replacement))
    {
        writeStatus (status, "The macOS update archive did not contain Light Host.app.");
        fs::remove_all (staging);
        return 1;
    }

    const fs::path backup = staging / "Light Host.previous.app";
    std::error_code error;
    fs::rename (target, backup, error);
    if (error)
    {
        writeStatus (status, "The installed app bundle could not be moved for updating: " + error.message());
        fs::remove_all (staging);
        return 1;
    }

    fs::rename (replacement, target, error);
    if (error)
    {
        std::error_code restoreError;
        fs::rename (backup, target, restoreError);
        if (restoreError)
        {
            writeStatus (status, "Could not install the new app bundle: " + error.message()
                                 + ". The previous app could not be restored: " + restoreError.message()
                                 + ". It remains at " + backup.string());
        }
        else
        {
            writeStatus (status, "Could not install the new app bundle: " + error.message()
                                 + ". The previous app was restored.");
            fs::remove_all (staging);
        }
        return 1;
    }

    fs::remove_all (backup, error);
    fs::remove_all (staging, error);
    writeStatus (status, "Light Host was updated successfully. macOS may block this unsigned app until it is signed and notarized.");
    if (! launchDetached ({ "/usr/bin/open", target.string() }))
    {
        writeStatus (status, "The update was installed, but Light Host could not be reopened.");
        return 1;
    }
    return 0;
}
#endif
}

int main (int argc, char** argv)
{
    if (argc != 6 && argc != 7)
        return 2;
    const std::string mode (argv[1]);
    const auto pid = static_cast<pid_t> (std::stol (argv[2]));
    const fs::path source (argv[3]);
    const fs::path target (argv[4]);
    const fs::path status (argv[5]);

    if (mode == "--linux")
        return installLinux (pid, source, target, status, argc == 7 ? fs::path (argv[6]) : fs::path());
#if defined(__APPLE__)
    if (mode == "--mac")
        return installMac (pid, source, target, status);
#endif
    return 2;
}
