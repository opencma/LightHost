#pragma once

#include <juce_core/juce_core.h>

inline juce::Result extractLinuxPackage (const juce::MemoryBlock& package, const juce::File& staging)
{
    juce::MemoryInputStream input (package, false);
    juce::ZipFile zip (input);
    if (zip.getNumEntries() != 2)
        return juce::Result::fail ("The package must contain exactly the app and updater.");
    for (const auto& name : { juce::String ("Light Host"), juce::String ("LightHostUpdater") })
    {
        const auto index = zip.getIndexOfFileName (name);
        const auto* entry = zip.getEntry (index);
        if (entry == nullptr || entry->isSymbolicLink || entry->uncompressedSize <= 0
            || entry->uncompressedSize > 512ll * 1024 * 1024)
            return juce::Result::fail ("The package has an invalid " + name + " entry.");
        std::unique_ptr<juce::InputStream> source (zip.createStreamForEntry (index));
        if (source == nullptr)
            return juce::Result::fail ("Could not open " + name + " in the package.");
        const auto outputFile = staging.getChildFile (name);
        juce::FileOutputStream output (outputFile);
        if (! output.openedOk()
            || output.writeFromInputStream (*source, entry->uncompressedSize) != entry->uncompressedSize)
            return juce::Result::fail ("Could not extract " + name + ".");
        output.flush();
        if (output.getStatus().failed())
            return juce::Result::fail ("Could not flush " + name + " to disk.");
        if (! outputFile.setExecutePermission (true))
            return juce::Result::fail ("Could not make " + name + " executable.");
    }
    return juce::Result::ok();
}

inline juce::Result installLinuxPackage (const juce::MemoryBlock& package, const juce::File& directory)
{
    if (! directory.createDirectory())
        return juce::Result::fail ("Could not create " + directory.getFullPathName());
    const auto staging = directory.getNonexistentChildFile (".install", {});
    if (! staging.createDirectory())
        return juce::Result::fail ("Could not create the installation staging directory.");
    auto result = extractLinuxPackage (package, staging);
    if (result.failed())
    {
        if (! staging.deleteRecursively())
            return juce::Result::fail (result.getErrorMessage() + "\nCould not remove " + staging.getFullPathName());
        return result;
    }

    const juce::String names[] { "Light Host", "LightHostUpdater" };
    bool saved[2] {}, installed[2] {};
    for (int i = 0; i < 2; ++i)
    {
        const auto target = directory.getChildFile (names[i]);
        const auto backup = staging.getChildFile ("previous-" + juce::String (i));
        if (target.isDirectory())
        {
            result = juce::Result::fail ("Cannot replace a directory at " + target.getFullPathName() + ".");
            break;
        }
        if (target.exists())
        {
            if (! target.moveFileTo (backup))
            {
                result = juce::Result::fail ("Could not move the existing " + names[i] + ".");
                break;
            }
            saved[i] = true;
        }
        if (! staging.getChildFile (names[i]).moveFileTo (target))
        {
            result = juce::Result::fail ("Could not install " + names[i] + ".");
            break;
        }
        installed[i] = true;
    }
    if (result.failed())
    {
        bool restored = true;
        for (int i = 1; i >= 0; --i)
        {
            const auto target = directory.getChildFile (names[i]);
            if (installed[i] && ! target.deleteFile())
                restored = false;
            if (saved[i] && ! staging.getChildFile ("previous-" + juce::String (i)).moveFileTo (target))
                restored = false;
        }
        if (! restored)
            return juce::Result::fail (result.getErrorMessage()
                                      + "\nCould not fully restore the previous installation; backups remain in "
                                      + staging.getFullPathName());
    }
    if (! staging.deleteRecursively())
        return juce::Result::fail ("Could not remove the installation staging directory: " + staging.getFullPathName());
    return result;
}
