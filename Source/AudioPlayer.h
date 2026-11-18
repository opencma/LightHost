#ifndef LIGHTHOST_AUDIO_PLAYER_H
#define LIGHTHOST_AUDIO_PLAYER_H

#include <atomic>
#include <cmath>
#include "WindowsAudioTiming.h"
#if JUCE_INTEL
 #include <xmmintrin.h>
#endif

// Audio callbacks only update counters. All diagnostic file I/O runs on the message thread.
class AudioPlayer : public AudioProcessorPlayer, private Timer
{
public:
    explicit AudioPlayer(const File& logFile)
        : logger(logFile, "Light Host audio diagnostics", maxLogBytes)
    {
#if JUCE_WINDOWS
        logger.logMessage(String("Windows background timer protection: ")
            + (windowsAudioTiming.isEnabled() ? "enabled" : "unavailable on this system"));
#endif
        startTimer(5000);
    }

    ~AudioPlayer() override
    {
        stopTimer();
        timerCallback();
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override
    {
        AudioProcessorPlayer::audioDeviceAboutToStart(device);
        lastCallbackFinished = 0.0;
        sampleRate.store(device->getCurrentSampleRate(), std::memory_order_relaxed);
        const ScopedLock eventGuard(eventLock);
        pendingEvent = "Stream started: " + device->getTypeName() + ", " + device->getName()
            + ", " + String(device->getCurrentSampleRate()) + " Hz, "
            + String(device->getCurrentBufferSizeSamples()) + " samples, "
            + String(device->getActiveInputChannels().countNumberOfSetBits()) + " inputs, "
            + String(device->getActiveOutputChannels().countNumberOfSetBits()) + " outputs";
    }

    void audioDeviceStopped() override
    {
        AudioProcessorPlayer::audioDeviceStopped();
        lastCallbackFinished = 0.0;
        sampleRate.store(0.0, std::memory_order_relaxed);
    }

    void audioDeviceError(const String& message) override
    {
        const ScopedLock eventGuard(eventLock);
        pendingError = message;
    }

    void audioDeviceIOCallback(const float** inputs, int inputCount, float** outputs,
                               int outputCount, int sampleCount) override
    {
        // Keep this around the entire plugin call: tiny filter/reverb tails can
        // enter the subnormal range during silence and make DSP miss its deadline,
        // producing crackles/robotic audio. Cleaning only the final output is too late.
        const ScopedFloatMode floatMode;
        const double started = Time::getMillisecondCounterHiRes();
        const double rate = sampleRate.load(std::memory_order_relaxed);
        const double bufferMs = rate > 0.0 ? 1000.0 * sampleCount / rate : 0.0;
        // Processing time alone misses stalls before the driver invokes us. Count
        // long idle gaps too, excluding our own DSP time and device restarts.
        if (bufferMs > 0.0 && lastCallbackFinished > 0.0 && started - lastCallbackFinished > 2.0 * bufferMs)
            callbackGaps.fetch_add(1, std::memory_order_relaxed);
        AudioProcessorPlayer::audioDeviceIOCallback(inputs, inputCount, outputs, outputCount, sampleCount);

        bool invalid = false;
        for (int channel = 0; channel < outputCount; ++channel)
            for (int sample = 0; sample < sampleCount; ++sample)
                if (!std::isfinite(outputs[channel][sample]))
                {
                    outputs[channel][sample] = 0.0f;
                    invalid = true;
                }
        if (invalid)
            invalidBuffers.fetch_add(1, std::memory_order_relaxed);

        lastCallbackFinished = Time::getMillisecondCounterHiRes();
        if (bufferMs > 0.0 && lastCallbackFinished - started > bufferMs)
            missedDeadlines.fetch_add(1, std::memory_order_relaxed);
    }

private:
    struct ScopedFloatMode
    {
        ScopedFloatMode() noexcept
        {
#if JUCE_INTEL
            savedMode = _mm_getcsr();
            _mm_setcsr(savedMode | 0x8040u); // Denormals-are-zero and flush-to-zero.
#endif
        }

        ~ScopedFloatMode() noexcept
        {
#if JUCE_INTEL
            // ASIO owns this thread; leave its floating-point environment intact.
            _mm_setcsr(savedMode);
#endif
        }

#if JUCE_INTEL
        unsigned int savedMode;
#endif
    };

    void timerCallback() override
    {
        String event, error;
        {
            const ScopedLock eventGuard(eventLock);
            event.swapWith(pendingEvent);
            error.swapWith(pendingError);
        }
        const int late = missedDeadlines.exchange(0, std::memory_order_relaxed);
        const int invalid = invalidBuffers.exchange(0, std::memory_order_relaxed);
        const int gaps = callbackGaps.exchange(0, std::memory_order_relaxed);
        if (event.isEmpty() && error.isEmpty() && late == 0 && invalid == 0 && gaps == 0)
            return;

        FileLogger::trimFileSize(logger.getLogFile(), maxLogBytes);
        const String timestamp = Time::getCurrentTime().toString(true, true) + " ";
        if (event.isNotEmpty())
            logger.logMessage(timestamp + event);
        if (error.isNotEmpty())
            logger.logMessage(timestamp + "Device error: " + error);
        if (late > 0 || invalid > 0)
            logger.logMessage(timestamp + "Callbacks exceeding buffer duration: " + String(late)
                + "; buffers containing non-finite output: " + String(invalid));
        if (gaps > 0)
            logger.logMessage(timestamp + "Gaps between callbacks exceeding two buffer durations: " + String(gaps));
    }

    static const int maxLogBytes = 128 * 1024;
#if JUCE_WINDOWS
    WindowsAudioTiming windowsAudioTiming;
#endif
    FileLogger logger;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> missedDeadlines { 0 }, invalidBuffers { 0 };
    std::atomic<int> callbackGaps { 0 };
    double lastCallbackFinished = 0.0; // Device callbacks are serialized by AudioDeviceManager.
    CriticalSection eventLock;
    String pendingEvent, pendingError;
};

#endif
