#pragma once

#include <juce_core/juce_core.h>

inline bool parseLightHostVersion (const juce::String& text, int (&version)[3])
{
    juce::StringArray parts;
    parts.addTokens (text, ".", "");
    if (parts.size() != 3)
        return false;

    for (int i = 0; i < 3; ++i)
    {
        if (parts[i].isEmpty() || parts[i].length() > 6
            || ! parts[i].containsOnly ("0123456789"))
            return false;
        version[i] = parts[i].getIntValue();
    }

    return true;
}

inline bool isNewerLightHostVersion (const int (&candidate)[3], const int (&current)[3])
{
    for (int i = 0; i < 3; ++i)
        if (candidate[i] != current[i])
            return candidate[i] > current[i];
    return false;
}

inline bool isValidLightHostReleaseAssetUrl (const juce::String& address)
{
    const juce::String prefix = "https://github.com/Anna-Spanner/LightHost/releases/download/";
    if (! address.startsWith (prefix))
        return false;

    juce::StringArray path;
    path.addTokens (address.substring (prefix.length()), "/", "");
    if (path.size() != 2)
        return false;

    for (const auto& part : path)
        if (part.isEmpty() || part == "." || part == ".."
            || ! part.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_."))
            return false;

    return true;
}

inline bool isValidLightHostSha256 (const juce::String& hash)
{
    return hash.length() == 64 && hash.containsOnly ("0123456789abcdefABCDEF");
}

inline bool getLightHostReleaseAsset (const juce::var& manifest, const juce::String& platform,
                                     juce::String& address, juce::String& hash)
{
    const auto assets = manifest.getProperty ("assets", {});
    const auto asset = assets.getProperty (platform, {});
    address = asset.getProperty ("url", {}).toString();
    hash = asset.getProperty ("sha256", {}).toString();
    return manifest.isObject() && assets.isObject() && asset.isObject()
        && isValidLightHostReleaseAssetUrl (address) && isValidLightHostSha256 (hash);
}
