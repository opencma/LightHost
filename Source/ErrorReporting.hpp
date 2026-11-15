#pragma once

#include <JuceHeader.h>

inline juce::String audioDeviceHelp()
{
    return "\n\nCheck the selected input and output and the audio device type. "
           "Some device combinations have no common sample rate; this can also fail "
           "if a device is unavailable or in use.\n\n"
           "For Windows audio, check both devices in Windows Sound settings > "
           "More sound settings > device Properties > Advanced, and try the same "
           "default format (for example 48000 Hz). In Light Host's audio settings, "
           "choose a common sample rate if available.\n\n"
           "For Voicemeeter Virtual Insert ASIO, use the same ASIO driver for both "
           "directions; its rate and buffer are controlled by Voicemeeter's A1 output.";
}

inline void showErrorWithDiagnostics (const juce::String& title, const juce::String& details)
{
    const auto stack = juce::SystemStats::getStackBacktrace();
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::WarningIcon, title,
        details + "\n\nDiagnostic stack at the point of reporting "
        "(not necessarily where the problem originated):\n"
        + (stack.isNotEmpty() ? stack : juce::String ("Not available.")));
}
