#pragma once

#include <JuceHeader.h>

inline bool parsePluginChannelMask (const juce::String& text, uint32& mask)
{
    if (text.isEmpty() || ! text.containsOnly ("0123456789"))
        return false;

    const auto value = text.getLargeIntValue();
    if (value < 0 || value > 0xff)
        return false;

    mask = (uint32) value;
    return true;
}

inline bool pluginUsesChannel (uint32 mask, int channel)
{
    return juce::isPositiveAndBelow (channel, 8) && (mask & (1u << channel)) != 0;
}
