#ifndef LIGHTHOST_WINDOWS_AUDIO_TIMING_H
#define LIGHTHOST_WINDOWS_AUDIO_TIMING_H

#if JUCE_WINDOWS
// JUCE requests 1 ms timers, but Windows 11 can ignore that request when the
// host is minimized. ASIO drivers running in this process still need precise
// waits even though Windows does not see playback through its own audio engine.
class WindowsAudioTiming
{
public:
    WindowsAudioTiming()
    {
        const auto getInformation = reinterpret_cast<ProcessInformationFunction>(
            kernel.getFunction("GetProcessInformation"));
        setInformation = reinterpret_cast<ProcessInformationFunction>(
            kernel.getFunction("SetProcessInformation"));
        const auto getProcess = reinterpret_cast<void* (__stdcall*)()>(kernel.getFunction("GetCurrentProcess"));
        if (getInformation == nullptr || setInformation == nullptr || getProcess == nullptr)
            return;
        process = getProcess();
        if (!getInformation(process, powerThrottlingInformation, &previous, sizeof(previous))) return;

        auto protectedState = previous;
        protectedState.controlMask |= ignoreTimerResolution;
        protectedState.stateMask &= ~ignoreTimerResolution;
        enabled = setInformation(process, powerThrottlingInformation,
                                 &protectedState, sizeof(protectedState)) != 0;
    }

    ~WindowsAudioTiming()
    {
        if (enabled)
            setInformation(process, powerThrottlingInformation, &previous, sizeof(previous));
    }

    bool isEnabled() const noexcept { return enabled; }

private:
    // Match PROCESS_POWER_THROTTLING_STATE and resolve the API at runtime, without
    // exposing Windows headers to shared audio/UI headers or requiring Windows 11.
    // https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
    enum { powerThrottlingInformation = 4, ignoreTimerResolution = 0x4 };
    struct PowerThrottlingState
    {
        uint32 version = 1, controlMask = 0, stateMask = 0;
    };
    using ProcessInformationFunction = int (__stdcall*)(void*, int, void*, unsigned long);
    DynamicLibrary kernel { "kernel32.dll" };
    ProcessInformationFunction setInformation = nullptr;
    void* process = nullptr;
    PowerThrottlingState previous;
    bool enabled = false;

    JUCE_DECLARE_NON_COPYABLE(WindowsAudioTiming)
};
#endif

#endif
