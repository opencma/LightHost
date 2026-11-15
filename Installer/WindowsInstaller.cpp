#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include "../Source/UpdateManifestValidation.hpp"

namespace
{
bool download (const juce::String& url, size_t limit, juce::MemoryBlock& data, std::wstring& error)
{
    int status = 0;
    auto stream = juce::URL (url).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (15000).withStatusCode (&status));
    if (stream == nullptr || status != 200)
    {
        error = L"Could not download from the update server (HTTP " + std::to_wstring (status) + L").";
        return false;
    }

    char buffer[65536];
    for (;;)
    {
        const auto count = stream->read (buffer, sizeof (buffer));
        if (count <= 0)
        {
            if (! stream->isExhausted())
            {
                error = L"The download was interrupted.";
                return false;
            }
            break;
        }
        if (data.getSize() + static_cast<size_t> (count) > limit)
        {
            error = L"The download exceeds the permitted size.";
            return false;
        }
        data.append (buffer, static_cast<size_t> (count));
    }
    if (data.getSize() == 0)
    {
        error = L"The downloaded file was empty.";
        return false;
    }
    return true;
}

bool downloadApplication (juce::MemoryBlock& application, std::wstring& error)
{
    juce::MemoryBlock response;
    if (! download ("https://annaspannertechsolutions.uk/lighthost/latest", 65536, response, error))
        return false;

    const auto manifest = juce::JSON::parse (juce::String::fromUTF8 (
        static_cast<const char*> (response.getData()), static_cast<int> (response.getSize())));
#if defined(_M_ARM64)
    const juce::String platform = "windows-arm64-package";
#else
    const juce::String platform = "windows-x64-package";
#endif
    juce::String address, hash;
    if (! getLightHostReleaseAsset (manifest, platform, address, hash))
    {
        error = L"The update manifest does not contain a valid Windows application package for this architecture.";
        return false;
    }
    if (! download (address, 512u * 1024u * 1024u, application, error))
        return false;
    if (! juce::SHA256 (application).toHexString().equalsIgnoreCase (hash))
    {
        error = L"The downloaded application's SHA-256 does not match the update manifest.";
        return false;
    }
    return true;
}

std::wstring systemError (DWORD error)
{
    wchar_t* buffer = nullptr;
    FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
                    reinterpret_cast<wchar_t*> (&buffer), 0, nullptr);
    std::wstring result = buffer != nullptr ? buffer : L"Unknown Windows error.";
    if (buffer != nullptr)
        LocalFree (buffer);
    return result;
}

bool ensureDirectory (const std::wstring& path, std::wstring& error)
{
    const auto status = SHCreateDirectoryExW (nullptr, path.c_str(), nullptr);
    if (status == ERROR_SUCCESS || status == ERROR_ALREADY_EXISTS || status == ERROR_FILE_EXISTS)
        return true;
    error = L"Could not create " + path + L": " + systemError (status);
    return false;
}

bool createLink (const std::wstring& executable, const std::wstring& directory,
                 REFKNOWNFOLDERID folder, std::wstring& error)
{
    PWSTR location = nullptr;
    HRESULT hr = SHGetKnownFolderPath (folder, KF_FLAG_CREATE, nullptr, &location);
    if (FAILED (hr))
    {
        error = L"Light Host was installed, but the shortcut folder could not be found.";
        return false;
    }
    const std::wstring linkPath = std::wstring (location) + L"\\Light Host.lnk";
    CoTaskMemFree (location);
    IShellLinkW* link = nullptr;
    hr = CoCreateInstance (CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&link));
    if (SUCCEEDED (hr))
    {
        hr = link->SetPath (executable.c_str());
        if (SUCCEEDED (hr)) hr = link->SetWorkingDirectory (directory.c_str());
        IPersistFile* persist = nullptr;
        if (SUCCEEDED (hr)) hr = link->QueryInterface (IID_PPV_ARGS (&persist));
        if (SUCCEEDED (hr))
        {
            hr = persist->Save (linkPath.c_str(), TRUE);
            persist->Release();
        }
        link->Release();
    }
    if (FAILED (hr))
    {
        error = L"Light Host was installed, but its shortcut could not be created: " + linkPath;
        return false;
    }
    return true;
}

bool install (const std::wstring& directory, bool createStartMenuShortcut,
              bool createDesktopShortcut, std::wstring& error)
{
    juce::MemoryBlock application;
    if (! downloadApplication (application, error))
        return false;
    if (! ensureDirectory (directory, error))
        return false;
    const auto data = application.getData();
    const auto size = static_cast<DWORD> (application.getSize());

    wchar_t temporary[MAX_PATH] {};
    if (GetTempFileNameW (directory.c_str(), L"lh", 0, temporary) == 0)
    {
        error = L"Could not create a temporary installation file: " + systemError (GetLastError());
        return false;
    }
    HANDLE file = CreateFileW (temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = L"Could not open the temporary installation file: " + systemError (GetLastError());
        DeleteFileW (temporary);
        return false;
    }
    DWORD written = 0;
    const bool success = WriteFile (file, data, size, &written, nullptr) != FALSE
                      && written == size && FlushFileBuffers (file) != FALSE;
    const DWORD writeError = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle (file);
    if (! success)
    {
        DeleteFileW (temporary);
        error = L"Could not write the application: " + systemError (writeError);
        return false;
    }
    const std::wstring executable = directory + L"\\Light Host.exe";
    if (! MoveFileExW (temporary, executable.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto moveError = GetLastError();
        DeleteFileW (temporary);
        error = L"Could not install or upgrade Light Host. Close all running Light Host instances and try again.\n\n"
                + systemError (moveError);
        return false;
    }
    if (createStartMenuShortcut && ! createLink (executable, directory, FOLDERID_Programs, error))
        return false;
    if (createDesktopShortcut && ! createLink (executable, directory, FOLDERID_Desktop, error))
        return false;
    return true;
}
}

int WINAPI wWinMain (HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    auto args = CommandLineToArgvW (GetCommandLineW(), &count);
    if (args == nullptr)
        return 1;
    std::wstring directory;
    bool shortcutsEnabled = true;
    bool valid = true;
    for (int i = 1; i < count; ++i)
    {
        const std::wstring arg = args[i];
        if (arg == L"--directory" && i + 1 < count)
            directory = args[++i];
        else if (arg == L"--no-shortcut")
            shortcutsEnabled = false;
        else
            valid = false;
    }
    LocalFree (args);
    if (! valid)
    {
        MessageBoxW (nullptr, L"Usage: Light Host Setup.exe [--directory path] [--no-shortcut]",
                     L"Light Host Setup", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (directory.empty())
    {
        PWSTR local = nullptr;
        if (FAILED (SHGetKnownFolderPath (FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
        {
            MessageBoxW (nullptr, L"Could not find the local application data folder.",
                         L"Light Host Setup", MB_OK | MB_ICONERROR);
            return 1;
        }
        directory = std::wstring (local) + L"\\Programs\\Light Host";
        CoTaskMemFree (local);
    }
    if (MessageBoxW (nullptr, (L"Download and install or upgrade Light Host in:\n\n" + directory
                               + L"\n\nExisting plugin settings will be kept. Close Light Host before continuing.").c_str(),
                     L"Light Host Setup", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return 2;
    bool startMenuShortcut = false, desktopShortcut = false;
    if (shortcutsEnabled)
    {
        const auto startChoice = MessageBoxW (nullptr, L"Create a Start menu shortcut?",
                                               L"Light Host Setup", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (startChoice == IDCANCEL)
            return 2;
        startMenuShortcut = startChoice == IDYES;
        const auto desktopChoice = MessageBoxW (nullptr, L"Create a desktop shortcut?",
                                                 L"Light Host Setup", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (desktopChoice == IDCANCEL)
            return 2;
        desktopShortcut = desktopChoice == IDYES;
    }
    const auto comResult = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED (comResult))
    {
        MessageBoxW (nullptr, L"Could not initialize Windows installer services.",
                     L"Light Host Setup", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::wstring error;
    const bool installed = install (directory, startMenuShortcut, desktopShortcut, error);
    CoUninitialize();
    if (! installed)
    {
        MessageBoxW (nullptr, error.c_str(), L"Light Host Setup", MB_OK | MB_ICONERROR);
        return 1;
    }
    MessageBoxW (nullptr, L"Light Host was installed successfully.", L"Light Host Setup",
                 MB_OK | MB_ICONINFORMATION);
    return 0;
}
