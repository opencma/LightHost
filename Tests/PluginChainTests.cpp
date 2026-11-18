#include "../JuceLibraryCode/JuceHeader.h"
#include "../Source/PluginChain.h"
#include "../Source/AudioSetup.h"
#include "../Source/AudioPlayer.h"
#include "../Source/PluginState.h"
#include "../Source/PluginBackup.h"
#include "../Source/PluginWindow.h"
#include "../Source/IconMenu.hpp"
#include "../Source/HostDesign.h"
#include "../Source/HostLookAndFeel.h"
#include "../Source/PluginLibraryView.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <thread>
#if JUCE_INTEL
 #include <xmmintrin.h>
#endif
#if JUCE_WINDOWS
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class GainProcessor : public AudioProcessor
{
public:
    explicit GainProcessor(float gain_) : gain(gain_)
    {
        setPlayConfigDetails(2, 2, 48000, 64);
    }

    const String getName() const override { return "Test gain"; }
    void prepareToPlay(double, int) override { ++prepares; }
    void releaseResources() override { ++releases; }
    void processBlock(AudioBuffer<float>& audio, MidiBuffer&) override
    {
        if (delayMs > 0)
            Thread::sleep(delayMs);
        ++calls;
        audio.applyGain(gain);
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    void getStateInformation(MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    int calls = 0;
    int prepares = 0, releases = 0;
    int delayMs = 0;

private:
    float gain;
};

void checkAudio(AudioProcessorGraph& graph, float expected)
{
    // Build the pending graph changes synchronously, without opening an audio device.
    graph.prepareToPlay(48000, 64);
    AudioBuffer<float> audio(2, 64);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 64; ++sample)
            audio.setSample(channel, sample, 0.25f);
    MidiBuffer midi;
    graph.processBlock(audio, midi);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 64; ++sample)
            require(std::abs(audio.getSample(channel, sample) - expected) < 0.0001f,
                    "Unexpected audio output");
}

void checkChain()
{
    AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000, 64);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);

    PluginChain::reconnect(graph, 0, [](int) { return false; });
    checkAudio(graph, 0.25f);

    auto* first = new GainProcessor(2.0f);
    auto* second = new GainProcessor(3.0f);
    graph.addNode(first, 1);
    graph.addNode(second, 2);
    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 1.5f);
    require(first->calls == 1 && second->calls == 1, "Enabled plugins must process audio");

    PluginChain::reconnect(graph, 2, [](int index) { return index == 0; });
    checkAudio(graph, 0.75f);
    require(first->calls == 1 && second->calls == 2, "Bypassed plugin consumed processing calls");

    PluginChain::reconnect(graph, 2, [](int) { return true; });
    checkAudio(graph, 0.25f);
    require(first->calls == 1 && second->calls == 2, "Bypassed chain consumed processing calls");

    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 1.5f);
    require(first->calls == 2 && second->calls == 3, "Plugins did not resume processing");
    require(graph.getNodeForId(1)->getProcessor() == first,
            "Bypass must preserve the plugin instance");

    graph.removeNode(1);
    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 0.75f);
    graph.removeNode(2);
    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 0.25f);
    graph.releaseResources();
}

void checkStableNodeRouting()
{
    AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000, 64);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);
    auto* first = new GainProcessor(2.0f);
    auto* second = new GainProcessor(3.0f);
    graph.addNode(first, 41);
    graph.addNode(second, 77);
    uint32 ids[] = { 41, 0, 77 };
    auto reconnect = [&](int bypass)
    {
        require(PluginChain::reconnect(graph, 3, [&](int i) { return i == bypass; },
            [&](int i) { return ids[i]; }).isEmpty(), "Stable node routing failed");
    };
    reconnect(-1);
    checkAudio(graph, 1.5f);
    std::swap(ids[0], ids[2]);
    reconnect(-1);
    checkAudio(graph, 1.5f);
    require(graph.isConnected(77, 41) && !graph.isConnected(41, 77), "Reorder ignored stable IDs");
    require(first->prepares == 1 && second->prepares == 1
            && first->releases == 0 && second->releases == 0, "Reorder reset existing DSP state");
    reconnect(0);
    checkAudio(graph, 0.5f);
    require(second->isSuspended() && !first->isSuspended(), "Bypass targeted the old list position");
    graph.removeNode(77);
    ids[0] = 0;
    reconnect(-1);
    checkAudio(graph, 0.5f);
    auto* third = new GainProcessor(4.0f);
    graph.addNode(third, 8);
    ids[1] = 8;
    reconnect(-1);
    checkAudio(graph, 2.0f);
    require(graph.getNodeForId(41)->getProcessor() == first && first->prepares == 1,
            "Adding/removing a node reset a retained plugin");
    graph.releaseResources();
}

void checkChannelSetup()
{
    XmlElement settings("DEVICESETUP");
    settings.setAttribute("audioDeviceInChans", "11111100");
    settings.setAttribute("audioDeviceOutChans", "1111");
    settings.setAttribute("audioDeviceRate", 48000);
    settings.setAttribute("audioDeviceBufferSize", 160);
    AudioSetup::limitSavedChannelsToStereo(settings);
    require(settings.getStringAttribute("audioDeviceInChans") == "1100",
            "Keep the selected input pair rather than switching to channels 1/2");
    require(settings.getStringAttribute("audioDeviceOutChans") == "11", "Limit output channels to stereo");
    require(settings.getIntAttribute("audioDeviceRate") == 48000
            && settings.getIntAttribute("audioDeviceBufferSize") == 160,
            "Channel migration changed rate or latency");
    settings.setAttribute("audioDeviceInChans", "100000");
    settings.setAttribute("audioDeviceOutChans", "0");
    AudioSetup::limitSavedChannelsToStereo(settings);
    require(settings.getStringAttribute("audioDeviceInChans") == "100000"
            && settings.getStringAttribute("audioDeviceOutChans") == "0", "Preserve mono and disabled channels");
    XmlElement defaults("DEVICESETUP");
    AudioSetup::limitSavedChannelsToStereo(defaults);
    require(!defaults.hasAttribute("audioDeviceInChans"), "Keep default channel selection implicit");
}

class TestDevice : public AudioIODevice
{
public:
    TestDevice() : AudioIODevice("Test device", "In-memory") {}
    StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    StringArray getInputChannelNames() override { return { "Left", "Right" }; }
    Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    Array<int> getAvailableBufferSizes() override { return { 64 }; }
    int getDefaultBufferSize() override { return 64; }
    String open(const BigInteger&, const BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    BigInteger getActiveOutputChannels() const override { return BigInteger(3); }
    BigInteger getActiveInputChannels() const override { return BigInteger(3); }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    int blockSize = 64;
    double sampleRate = 48000.0;
};

void checkMinimizedAudioTiming()
{
#if JUCE_WINDOWS
    using InformationFunction = BOOL (WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto read = reinterpret_cast<InformationFunction>(GetProcAddress(kernel, "GetProcessInformation"));
    const auto write = reinterpret_cast<InformationFunction>(GetProcAddress(kernel, "SetProcessInformation"));
    PROCESS_POWER_THROTTLING_STATE original = { PROCESS_POWER_THROTTLING_CURRENT_VERSION, 0, 0 };
    if (read == nullptr || write == nullptr
        || !read(GetCurrentProcess(), ProcessPowerThrottling, &original, sizeof(original)))
    {
        std::cout << "SKIP: Windows background timer policy API is unavailable\n";
        return;
    }
    struct RestorePolicy
    {
        InformationFunction write;
        PROCESS_POWER_THROTTLING_STATE original;
        ~RestorePolicy() { write(GetCurrentProcess(), ProcessPowerThrottling, &original, sizeof(original)); }
    } restorePolicy { write, original };

    auto throttled = original;
    throttled.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    throttled.StateMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    if (!write(GetCurrentProcess(), ProcessPowerThrottling, &throttled, sizeof(throttled)))
    {
        std::cout << "SKIP: Windows does not support background timer throttling\n";
        return;
    }

    DocumentWindow window("Audio timing regression", Colours::black, DocumentWindow::allButtons);
    window.setUsingNativeTitleBar(true);
    window.setBounds(50, 50, 400, 300);
    window.setVisible(true);
    window.setMinimised(true);
    MessageManager::getInstance()->runDispatchLoopUntil(20);
    require(window.isMinimised(), "Native timing test window did not minimize");
    const auto measureWait = []
    {
        const double started = Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 24; ++i) Thread::sleep(1);
        return (Time::getMillisecondCounterHiRes() - started) / 24.0;
    };
    const double throttledWait = measureWait();
    const File logFile = File::createTempFile(".log");
    {
        AudioPlayer player(logFile);
        TestDevice device;
        GainProcessor processor(2.0f);
        player.audioDeviceAboutToStart(&device);
        player.setProcessor(&processor);
        AudioBuffer<float> input(2, 64), output(2, 64);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i) input.setSample(channel, i, 0.25f);
        const auto checkProcessing = [&]
        {
            PROCESS_POWER_THROTTLING_STATE state = { PROCESS_POWER_THROTTLING_CURRENT_VERSION, 0, 0 };
            require(read(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))
                    && (state.ControlMask & PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION) != 0
                    && (state.StateMask & PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION) == 0,
                    "Audio player did not protect high-resolution timers from background throttling");
            require((state.ControlMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
                        == (original.ControlMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
                    && (state.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
                        == (original.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED),
                    "Timer protection changed an unrelated Windows power policy");
            player.audioDeviceIOCallback(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 64);
            require(output.getSample(0, 0) == 0.5f && output.getSample(1, 63) == 0.5f,
                    "A native window state changed audio processing");
        };
        checkProcessing(); // Minimized in the taskbar.
        const double protectedWait = measureWait();
        window.setMinimised(false);
        MessageManager::getInstance()->runDispatchLoopUntil(20);
        checkProcessing();
        window.setVisible(false); // Closed to the tray.
        checkProcessing();
        require(processor.prepares == 1 && processor.releases == 0,
                "Window minimize/restore/hide restarted the audio processor");
        player.audioDeviceStopped();
        player.setProcessor(nullptr);
        std::cout << "PASS: native minimized/restored/hidden windows retain timer protection and audio; mean 1 ms wait: "
                  << throttledWait << " ms throttled, " << protectedWait << " ms protected\n";
    }
    PROCESS_POWER_THROTTLING_STATE restored = { PROCESS_POWER_THROTTLING_CURRENT_VERSION, 0, 0 };
    require(read(GetCurrentProcess(), ProcessPowerThrottling, &restored, sizeof(restored))
            && restored.ControlMask == throttled.ControlMask && restored.StateMask == throttled.StateMask,
            "Audio player shutdown did not restore the previous Windows timer policy");
    require(logFile.deleteFile(), "Could not clean up timing test diagnostics");
#endif
}

void checkAudioDenormals()
{
#if JUCE_INTEL
    class TailProcessor : public GainProcessor
    {
    public:
        TailProcessor() : GainProcessor(2.0f) {}
        void processBlock(AudioBuffer<float>& audio, MidiBuffer& midi) override
        {
            GainProcessor::processBlock(audio, midi);
            // A normal input decaying into a subnormal tests FTZ as well as DAZ.
            volatile float tail = std::numeric_limits<float>::min();
            audio.setSample(1, 1, tail * 0.5f);
        }
    };
    const File logFile = File::createTempFile(".log");
    {
        TestDevice device;
        TailProcessor processor;
        AudioPlayer player(logFile);
        player.audioDeviceAboutToStart(&device);
        player.setProcessor(&processor);
        AudioBuffer<float> input(2, 64), output(2, 64);
        input.clear();
        input.setSample(0, 0, std::numeric_limits<float>::denorm_min());
        input.setSample(1, 0, -std::numeric_limits<float>::denorm_min());
        input.setSample(0, 1, 0.25f);

        // Simulate a driver thread that has not enabled DAZ/FTZ. Protection must
        // cover the plugin's arithmetic, not just scrub its output afterwards.
        const unsigned int originalMode = _mm_getcsr();
        _mm_setcsr(originalMode & ~0x8040u);
        player.audioDeviceIOCallback(input.getArrayOfReadPointers(), 2,
                                     output.getArrayOfWritePointers(), 2, 64);
        const bool restored = (_mm_getcsr() & 0x8040u) == 0;
        _mm_setcsr(originalMode);
        require(output.getSample(0, 0) == 0.0f && output.getSample(1, 0) == 0.0f,
                "Subnormal samples reached plugin arithmetic without denormal protection");
        require(output.getSample(1, 1) == 0.0f, "Plugin tails were not flushed out of the subnormal range");
        require(output.getSample(0, 1) == 0.5f, "Denormal protection changed normal audio");
        require(restored, "Audio callback changed the driver's floating-point mode");
        player.audioDeviceStopped();
        player.setProcessor(nullptr);
    }
    logFile.deleteFile();
#endif
}

void checkPlayerDiagnostics()
{
    const File logFile = File::createTempFile(".log");
    {
        TestDevice device;
        GainProcessor processor(std::numeric_limits<float>::infinity());
        GainProcessor normalProcessor(2.0f);
        AudioPlayer player(logFile);
        processor.delayMs = 10; // Deliberately miss the 64-sample deadline.
        player.audioDeviceAboutToStart(&device);
        player.setProcessor(&processor);
        AudioBuffer<float> input(2, 64), output(2, 64);
        input.clear();
        output.clear();
        // Infinity times zero generates NaNs; they must not reach the device.
        player.audioDeviceIOCallback(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 64);
        require(output.getMagnitude(0, 64) == 0.0f, "Invalid output was not silenced");
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 64; ++sample)
                require(std::isfinite(output.getSample(channel, sample)), "NaN escaped into the output");

        player.setProcessor(&normalProcessor);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 64; ++sample)
                input.setSample(channel, sample, 0.25f);
        player.audioDeviceIOCallback(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 64);
        require(output.getSample(0, 0) == 0.5f && output.getSample(1, 63) == 0.5f,
                "Diagnostics changed valid audio");

        // A driver/scheduler stall happens outside processBlock and must still
        // be visible even when every individual callback finishes on time.
        Thread::sleep(20);
        player.audioDeviceIOCallback(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 64);

        player.audioDeviceStopped();
        device.sampleRate = 96000.0;
        device.blockSize = 128;
        player.audioDeviceAboutToStart(&device);
        require(normalProcessor.getSampleRate() == 96000.0 && normalProcessor.getBlockSize() == 128,
                "Device restart did not propagate the new rate and buffer size");
        player.audioDeviceError("Test device failure");
        player.audioDeviceStopped();
        player.setProcessor(nullptr);
    }
    const String log = logFile.loadFileAsString();
    require(log.contains("96000") && log.contains("128 samples"), "Missing stream format diagnostics");
    require(log.contains("Device error: Test device failure"), "Device errors were lost");
    require(log.contains("buffers containing non-finite output: 1"), "Invalid-output diagnostic was lost");
    require(log.contains("Callbacks exceeding buffer duration:")
            && !log.contains("Callbacks exceeding buffer duration: 0"), "Missed processing deadline was not logged");
    require(log.contains("Gaps between callbacks exceeding two buffer durations:"),
            "A stall outside the audio callback was not logged");
    logFile.deleteFile();
}

void checkIdentityAndMigration()
{
    PluginDescription first;
    first.name = "Same Name";
    first.version = "1.0";
    first.pluginFormatName = "VST3";
    first.fileOrIdentifier = "/plugins/first.vst3";
    first.uid = 1;
    PluginDescription second(first);
    second.fileOrIdentifier = "/plugins/second.vst3";
    second.uid = 2;
    KnownPluginList known, active;
    known.addType(first);
    active.addType(first);
    active.addType(second);
    const auto plugins = PluginState::collectPlugins(known, active);
    require(plugins.size() == 2, "Export omitted an active-only plugin or duplicated a known one");

    PropertySet settings;
    settings.setValue(PluginState::legacyKey("state", first), "old shared state");
    settings.setValue(PluginState::legacyKey("order", first), 7);
    settings.setValue(PluginState::legacyKey("bypass", first), true);
    settings.setValue(PluginState::legacyKey("display-name", first), "Custom name");
    PluginState::migrateLegacySettings(settings, plugins);
    require(PluginState::key("state", first) != PluginState::key("state", second), "State keys still collide");
    PluginDescription sameContainer(first);
    sameContainer.uid = 2;
    require(PluginState::key("state", first) != PluginState::key("state", sameContainer), "Shell plugin IDs share settings");
    PluginDescription otherFormat(first);
    otherFormat.pluginFormatName = "VST";
    require(PluginState::key("state", first) != PluginState::key("state", otherFormat), "Plugin formats share settings");
    PluginDescription updated(first);
    updated.name = "Renamed effect";
    updated.version = "2.0";
    PluginDescription differentCase(first);
    differentCase.fileOrIdentifier = "/plugins/First.vst3";
    require(PluginState::sameIdentity(first, updated)
            && !PluginState::sameIdentity(first, second)
            && !PluginState::sameIdentity(first, sameContainer)
            && !PluginState::sameIdentity(first, otherFormat)
            && !PluginState::sameIdentity(first, differentCase),
            "Direct identity comparisons must match persisted keys, including case-sensitive paths");
    require(settings.getValue(PluginState::key("state", first)) == "old shared state"
            && settings.getValue(PluginState::key("state", second)) == "old shared state",
            "Legacy collision migration lost existing settings");
    settings.setValue(PluginState::key("state", first), "independent first state");
    require(settings.getValue(PluginState::key("state", second)) == "old shared state", "Plugin settings are not independent");
    require(settings.getBoolValue(PluginState::key("bypass", first))
            && settings.getIntValue(PluginState::key("order", first)) == 7
            && settings.getValue(PluginState::key("display-name", first)) == "Custom name", "Legacy settings were not preserved");
    settings.removeValue(PluginState::key("state", first));
    PluginState::migrateLegacySettings(settings, plugins);
    require(!settings.containsKey(PluginState::key("state", first)), "Deleted state was resurrected from a legacy key");

    PluginDescription moved(second);
    moved.fileOrIdentifier = "/another-machine/second.vst3";
    settings.setValue(PluginState::key("display-name", second), "Portable name");
    PluginState::remapSettings(settings, second, moved);
    require(!settings.containsKey(PluginState::key("state", second))
            && settings.getValue(PluginState::key("state", moved)) == "old shared state"
            && settings.getValue(PluginState::key("display-name", moved)) == "Portable name", "Restored paths lost plugin settings");
    const String beforeUpdate = PluginState::key("state", moved);
    moved.name = "Updated Name";
    moved.version = "2.0";
    require(PluginState::key("state", moved) == beforeUpdate, "Updating metadata lost plugin identity");

    PropertySet alreadyMigrated;
    alreadyMigrated.setValue(PluginState::legacyKey("state", first), "obsolete");
    alreadyMigrated.setValue(PluginState::key("state", first), "current");
    PluginState::migrateLegacySettings(alreadyMigrated, { first });
    require(alreadyMigrated.getValue(PluginState::key("state", first)) == "current", "Migration overwrote newer state");

    PropertySet legacyBackup;
    legacyBackup.setValue(PluginState::legacyKey("state", second), "legacy backup state");
    legacyBackup.setValue(PluginState::legacyKey("display-name", second), "Legacy portable name");
    PluginDescription restored(second);
    restored.fileOrIdentifier = "/restored/second.vst3";
    PluginState::remapSettings(legacyBackup, second, restored);
    PluginState::migrateLegacySettings(legacyBackup, { restored });
    require(legacyBackup.getValue(PluginState::key("state", restored)) == "legacy backup state"
            && legacyBackup.getValue(PluginState::key("display-name", restored)) == "Legacy portable name",
            "An original backup lost state or display names after moving paths");
}

void checkBackupRoundTrip(const File& root, const String& name, bool bundle, bool legacy)
{
    const File original = root.getChildFile("source/" + name);
    if (bundle)
    {
        require(original.getChildFile("Contents").createDirectory().wasOk(), "Could not create bundle fixture");
        require(original.getChildFile("Contents/binary").replaceWithText("bundle data"), "Could not write bundle fixture");
    }
    else
    {
        require(original.getParentDirectory().createDirectory().wasOk(), "Could not create file fixture");
        require(original.replaceWithText("file data"), "Could not write file fixture");
    }
    ZipFile::Builder builder;
    require(PluginBackup::addFiles(builder, original, "plugins/0"), "Could not archive plugin fixture");
    MemoryOutputStream bytes;
    require(builder.writeToStream(bytes, nullptr), "Could not write fixture ZIP");
    MemoryInputStream input(bytes.getData(), bytes.getDataSize(), false);
    ZipFile archive(input);
    XmlElement manifest("PLUGIN");
    if (!legacy)
        manifest.setAttribute("isDirectory", bundle);
    const bool directory = PluginBackup::isDirectoryArchive(manifest, archive, "plugins/0/", name);
    require(directory == bundle, "Wrong layout inferred for backup");
    const File destination = root.getChildFile(legacy ? "legacy/" + name : "current/" + name);
    for (int i = 0; i < archive.getNumEntries(); ++i)
    {
        const String relative = archive.getEntry(i)->filename.substring(String("plugins/0/").length());
        const File target = PluginBackup::extractionTarget(destination, relative, directory);
        require(target != File() && target.getParentDirectory().createDirectory().wasOk(), "Invalid restore target");
        ScopedPointer<InputStream> stream(archive.createStreamForEntry(i));
        FileOutputStream output(target);
        require(stream != nullptr && !output.failedToOpen(), "Could not extract fixture");
        require(output.writeFromInputStream(*stream, -1) == archive.getEntry(i)->uncompressedSize, "Incomplete restore");
        output.flush();
        require(output.getStatus().wasOk(), "Restore write failed");
    }
    if (bundle)
        require(destination.isDirectory() && destination.getChildFile("Contents/binary").loadFileAsString() == "bundle data", "Bundle restore failed");
    else
        require(destination.existsAsFile() && destination.loadFileAsString() == "file data", "Single-file restore nested the binary in a directory");
    require(PluginBackup::extractionTarget(destination, "../escape", directory) == File(), "Unsafe archive path accepted");
}

void checkBackups()
{
    const File oldApp = File::getSpecialLocation(File::tempDirectory).getChildFile("old-app");
    const File newApp = oldApp.getSiblingFile("new-app");
    PluginDescription portable;
    portable.name = "Portable";
    portable.pluginFormatName = "VST3";
    portable.uid = 42;
    portable.fileOrIdentifier = oldApp.getChildFile("VST Plugins/import/Portable.vst3").getFullPathName();
    KnownPluginList portableList;
    portableList.addType(portable);
    ScopedPointer<XmlElement> portableXml(portableList.createXml());
    PropertySet portableSettings;
    portableSettings.setValue("pluginList", portableXml);
    portableSettings.setValue("pluginListActive", portableXml);
    portableSettings.setValue("portableAppDirectory", oldApp.getFullPathName());
    portableSettings.setValue("lastPluginScanPath_VST3", oldApp.getChildFile("VST Plugins/import").getFullPathName());
    for (const char* type : { "state", "order", "bypass", "display-name" })
        portableSettings.setValue(PluginState::key(type, portable), "preserved");
    PluginState::relocatePortableSettings(portableSettings, newApp);
    PluginDescription relocated(portable);
    relocated.fileOrIdentifier = newApp.getChildFile("VST Plugins/import/Portable.vst3").getFullPathName();
    for (const char* type : { "state", "order", "bypass", "display-name" })
        require(portableSettings.getValue(PluginState::key(type, relocated)) == "preserved"
                && !portableSettings.containsKey(PluginState::key(type, portable)),
                "Moving app directory lost plugin settings");
    for (const char* listKey : { "pluginList", "pluginListActive" })
    {
        ScopedPointer<XmlElement> xml(XmlDocument::parse(portableSettings.getValue(listKey)));
        KnownPluginList movedList;
        movedList.recreateFromXml(*xml);
        require(movedList.getNumTypes() == 1
                && movedList.getType(0)->fileOrIdentifier == relocated.fileOrIdentifier,
                "Moving app directory did not relocate a plugin list");
    }
    require(portableSettings.getValue("lastPluginScanPath_VST3")
            == newApp.getChildFile("VST Plugins/import").getFullPathName(), "Scan path did not follow executable");
    PluginState::relocatePortableSettings(portableSettings, newApp);
    require(portableSettings.getValue(PluginState::key("state", relocated)) == "preserved",
            "Repeated portable startup lost state");
    PropertySet settings;
    require(PluginBackup::hasValidPluginLists(settings), "Empty installation rejected");
    settings.setValue("pluginListActive", "broken XML");
    require(!PluginBackup::hasValidPluginLists(settings), "Broken active list accepted");
    settings.setValue("pluginListActive", "<PROPERTIES/>");
    require(!PluginBackup::hasValidPluginLists(settings), "Wrong list root accepted");
    settings.setValue("pluginListActive", "<KNOWNPLUGINS/>");
    settings.setValue("icon", "white");
    settings.setValue("plugin-state-example", "binary+/= data");
    ScopedPointer<XmlElement> saved(settings.createXml("PROPERTIES"));
    ScopedPointer<XmlElement> parsed(XmlDocument::parse(saved->createDocument(String(), true)));
    PropertySet restored;
    require(parsed != nullptr, "Settings XML round trip failed");
    restored.restoreFromXml(*parsed);
    require(PluginBackup::hasValidPluginLists(restored)
            && restored.getValue("icon") == "white"
            && restored.getValue("plugin-state-example") == "binary+/= data",
            "Settings values lost in XML round trip");
    require(PluginBackup::supportsVersion(1) && PluginBackup::supportsVersion(2)
            && PluginBackup::supportsVersion(3) && !PluginBackup::supportsVersion(4),
            "Backup version compatibility regressed");
    const File parent = File::getSpecialLocation(File::tempDirectory);
    const File root = parent.getNonexistentChildFile("LightHost-backup-tests", "", false);
    require(root.isAChildOf(parent) && root.createDirectory().wasOk(), "Invalid temporary test directory");
    for (bool legacy : { false, true })
    {
        checkBackupRoundTrip(root, "Example.dll", false, legacy);
        checkBackupRoundTrip(root, "Example.vst3", false, legacy);
        checkBackupRoundTrip(root, "Bundle.vst", true, legacy);
    }
    {
        const File output = root.getChildFile("worker.lightbak");
        PluginBackup::ExportTask worker(output);
        worker.plugins.add(root.getChildFile("source/Example.dll"));
        worker.plugins.add(root.getChildFile("source/Bundle.vst"));
        worker.settingsText = "<PROPERTIES/>";
        worker.manifestText = "<LIGHTHOST_BACKUP version=\"2\"/>";
        worker.startThread(3);
        require(worker.waitForThreadToExit(10000) && worker.succeeded && worker.error.isEmpty(),
                "Background backup failed");
        ZipFile archive(output);
        for (const char* name : { "plugins/0/Example.dll", "plugins/1/Contents/binary", "settings.xml", "manifest.xml" })
        {
            const int index = archive.getIndexOfFileName(name);
            require(index >= 0, "Background backup omitted an entry");
            ScopedPointer<InputStream> input(archive.createStreamForEntry(index));
            const String expected = String(name) == "settings.xml" ? worker.settingsText
                : String(name) == "manifest.xml" ? worker.manifestText
                : String(name).endsWith("Example.dll") ? "file data" : "bundle data";
            require(input != nullptr && input->readEntireStreamAsString() == expected,
                    "Background backup corrupted an entry");
        }
        PluginBackup::ExportTask cancelled(root.getChildFile("cancelled.lightbak"));
        cancelled.plugins.add(root.getChildFile("source/Example.dll"));
        cancelled.signalThreadShouldExit();
        cancelled.run();
        require(!cancelled.succeeded && !root.getChildFile("cancelled.lightbak").exists(),
                "Cancelled backup was written");
        PluginBackup::ExportTask missingFile(root.getChildFile("failed.lightbak"));
        missingFile.plugins.add(root.getChildFile("not-installed.dll"));
        missingFile.run();
        require(!missingFile.succeeded && missingFile.error.isNotEmpty(), "Missing backup source was accepted");
    }
    const File missing = root.getChildFile("missing/Example.dll");
    const File restoreRoot = root.getChildFile("restored");
    FileSearchPath paths;
    paths.add(root.getChildFile("system-folder"));
    const File fresh = PluginBackup::restoreDestination(missing, paths, restoreRoot);
    require(fresh.isAChildOf(restoreRoot) && fresh.getFileName() == "Example.dll",
            "Missing plugins must restore outside system scan folders");
    require(fresh.getParentDirectory().createDirectory().wasOk()
            && fresh.replaceWithText("partial"), "Could not create interrupted restore fixture");
    require(PluginBackup::restoreDestination(missing, paths, restoreRoot) != fresh,
            "Interrupted restore reused as a complete installation");
    const File installed = root.getChildFile("source/Example.dll");
    require(PluginBackup::restoreDestination(installed, paths, restoreRoot) == installed,
            "Original installation was not reused");
    paths.add(installed.getParentDirectory());
    require(PluginBackup::restoreDestination(missing, paths, restoreRoot) == installed,
            "Installation in another scan folder was not reused");
    require(!PluginBackup::isSafePath("folder/.. /escape")
            && !PluginBackup::isSafePath("/absolute")
            && !PluginBackup::isSafePath("C:/absolute"), "Unsafe path validation regressed");
    require(root.isAChildOf(parent) && root.deleteRecursively(), "Could not clean up the owned test directory");
}

void checkUnsupportedLayoutAndEditor()
{
    AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000, 64);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);
    auto* mono = new GainProcessor(2.0f);
    mono->setPlayConfigDetails(1, 1, 48000, 64);
    const auto node = graph.addNode(mono, 1);
    const StringArray issues = PluginChain::reconnect(graph, 1, [](int) { return false; });
    require(!issues.isEmpty(), "Unsupported layout was silently accepted");
    checkAudio(graph, 0.25f);
    require(mono->calls == 0 && mono->isSuspended(), "Unsupported plugin was still processed");
    PluginWindow* first = PluginWindow::getWindowFor(node, PluginWindow::Normal);
    require(first != nullptr && !first->isVisible(), "Editor must be constructed before presentation");
    PluginWindow* second = PluginWindow::getWindowFor(node, PluginWindow::Normal);
    const bool reused = first == second;
    if (!reused)
        delete second;
    const auto otherNode = graph.addNode(new GainProcessor(1.0f), 55);
    PluginWindow* other = PluginWindow::getWindowFor(otherNode, PluginWindow::Normal);
    PluginWindow::closeWindowsFor(node);
    require(reused && PluginWindow::containsActiveWindows()
            && PluginWindow::getWindowFor(otherNode, PluginWindow::Normal) == other,
            "Removing one plugin closed an unrelated editor");
    PluginWindow::closeAllCurrentlyOpenWindows();
    require(!PluginWindow::containsActiveWindows(), "Editor cleanup leaked a window");
    graph.releaseResources();
}

class StateCaptureProcessor : public GainProcessor
{
public:
    StateCaptureProcessor() : GainProcessor(1.0f) { delayMs = 40; }
    void processBlock(AudioBuffer<float>& audio, MidiBuffer& midi) override
    {
        processing.store(true);
        entered.signal();
        GainProcessor::processBlock(audio, midi);
        processing.store(false);
    }
    void getStateInformation(MemoryBlock& state) override
    {
        overlapped = processing.load();
        state.append("saved", 5);
    }
    WaitableEvent entered;
    std::atomic<bool> processing { false };
    bool overlapped = false;
};

void checkSynchronizedStateCapture()
{
    AudioProcessorGraph graph;
    StateCaptureProcessor processor;
    std::thread audio([&]
    {
        const ScopedLock graphGuard(graph.getCallbackLock());
        const ScopedLock callbackGuard(processor.getCallbackLock());
        AudioBuffer<float> buffer(2, 64);
        buffer.clear();
        MidiBuffer midi;
        processor.processBlock(buffer, midi);
    });
    const bool entered = processor.entered.wait(2000);
    MemoryBlock state;
    if (entered)
        PluginState::capture(graph, processor, state);
    audio.join();
    require(entered && !processor.overlapped && state.getSize() == 5,
            "State capture overlapped processing or lost the snapshot");
}

void checkPluginLibraryActions()
{
    PluginDescription first;
    first.name = "Equalizer";
    first.manufacturerName = "First vendor";
    first.pluginFormatName = "VST3";
    first.fileOrIdentifier = "/plugins/first.vst3";
    first.uid = 1;
    PluginDescription second(first);
    second.manufacturerName = "Second vendor";
    second.fileOrIdentifier = "/plugins/second.vst3";
    second.uid = 2;
    second.category = "Fx|Dynamics";
    second.version = "2.4.1";
    second.descriptiveName = "Precise compression";
    KnownPluginList known;
    PluginInstances active;
    known.addType(first);
    known.addType(second);
    PluginLibraryView library(known, active);
    library.setSize(300, 500);
    ListBox* list = nullptr;
    TextEditor* search = nullptr;
    for (int i = 0; i < library.getNumChildComponents(); ++i)
    {
        if (auto* child = dynamic_cast<ListBox*>(library.getChildComponent(i))) list = child;
        if (auto* child = dynamic_cast<TextEditor*>(library.getChildComponent(i))) search = child;
    }
    require(list != nullptr && search != nullptr && list->getModel()->getNumRows() == 2,
            "Plugin library did not expose its installed effects");
    std::function<void(Component&)> checkNoTooltips = [&](Component& component)
    {
        if (auto* client = dynamic_cast<TooltipClient*>(&component))
            require(client->getTooltip().isEmpty(), "A library control retained a tooltip");
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            checkNoTooltips(*component.getChildComponent(i));
    };
    checkNoTooltips(library);
    for (const char* query : { "Dynamics", "2.4.1", "Precise compression" })
    {
        search->setText(query, false);
        library.refresh();
        require(list->getModel()->getNumRows() == 1,
                "Inline library details were not searchable");
        require(list->getModel()->getTooltipForRow(0).isEmpty(),
                "Plugin library still exposes hover tooltips");
    }
    int additions = 0;
    String addedIdentity;
    library.onAddPlugin = [&](const PluginDescription& plugin)
    {
        ++additions;
        addedIdentity = PluginState::key("library", plugin);
        active.addType(plugin);
    };
    search->setText("SECOND VENDOR", false);
    library.refresh();
    require(list->getModel()->getNumRows() == 1, "Library search did not match manufacturer without case sensitivity");
    list->selectRow(0);
    list->keyPressed(KeyPress(KeyPress::returnKey));
    require(additions == 1 && addedIdentity == PluginState::key("library", second),
            "Adding a filtered result selected the wrong same-name plugin");
    list->keyPressed(KeyPress(KeyPress::returnKey));
    require(additions == 2 && active.getNumTypes() == 2
            && active.getType(0)->instanceId != active.getType(1)->instanceId,
            "Library did not add independent copies of the same effect");
    list->keyPressed(KeyPress(KeyPress::deleteKey));
    require(known.getNumTypes() == 1 && active.getNumTypes() == 2
            && list->getModel()->getNumRows() == 0,
            "Removing a filtered library entry removed active copies or targeted another plugin");
    known.addType(second);
    library.refresh();
    list->selectRow(0);
    active.clear();
    for (int i = known.getNumTypes(); --i >= 0;)
        if (PluginState::key("library", *known.getType(i)) == PluginState::key("library", second))
            known.removeType(i); // The displayed snapshot stays stale until its change notification arrives.
    list->keyPressed(KeyPress(KeyPress::returnKey));
    require(additions == 2, "A stale library row added a plugin removed by library management");
    search->setText("no match", false);
    library.refresh();
    require(list->getModel()->getNumRows() == 0 && list->getSelectedRow() == -1,
            "Search retained an actionable selection after all results disappeared");
    search->clear();
    library.refresh();
    require(list->getModel()->getNumRows() == 1 && list->getSelectedRow() == -1,
            "Clearing search redirected the old selection to another plugin");
}

void checkBypassPresentation()
{
    require(HostDesign::effectStatus(true, false) == "Active"
            && HostDesign::effectStatus(true, true) == "Bypassed",
            "Loaded effects did not expose their bypass state");
    require(HostDesign::effectStatus(false, false) == "Unavailable"
            && HostDesign::effectStatus(false, true) == "Bypassed / Unavailable",
            "A missing plugin concealed its saved bypass state");
    for (bool available : { false, true })
        require(HostDesign::powerColour(available, false) != HostDesign::powerColour(available, true),
                "The power control did not change appearance when bypass was toggled");
}

void checkHostCursors()
{
    LightHostLookAndFeel theme;
    TextButton button;
    Label buttonLabel;
    button.addAndMakeVisible(buttonLabel);
    require(theme.getMouseCursorFor(button) == MouseCursor::PointingHandCursor
            && theme.getMouseCursorFor(buttonLabel) == MouseCursor::PointingHandCursor,
            "Clickable buttons or their labels do not show a hand");
    button.setEnabled(false);
    require(theme.getMouseCursorFor(buttonLabel) == MouseCursor::NormalCursor,
            "Disabled button still shows a clickable cursor");
    ComboBox combo;
    require(theme.getMouseCursorFor(combo) == MouseCursor::PointingHandCursor
            && theme.getMouseCursorFor(*combo.getChildComponent(0)) == MouseCursor::PointingHandCursor,
            "Device selectors or their labels do not show a hand");
    HostDesign::Switch toggle("Dark mode");
    require(theme.getMouseCursorFor(toggle) == MouseCursor::PointingHandCursor,
            "Settings switch does not show a hand");
    TextEditor editor;
    require(theme.getMouseCursorFor(editor) == MouseCursor::IBeamCursor,
            "Text field lost its text insertion cursor");
    Component background;
    require(theme.getMouseCursorFor(background) == MouseCursor::NormalCursor,
            "Noninteractive background shows a clickable cursor");
    background.setMouseCursor(MouseCursor::LeftRightResizeCursor);
    require(theme.getMouseCursorFor(background) == MouseCursor::LeftRightResizeCursor,
            "Host styling replaced a resize cursor");
}

void checkAppearanceChanges()
{
    const bool previous = HostDesign::isDarkMode();
    LightHostLookAndFeel theme;
    KnownPluginList known;
    PluginInstances active;
    PluginLibraryView library(known, active);
    library.setLookAndFeel(&theme);
    library.setSize(380, 560);
    library.addToDesktop(0);
    auto* list = dynamic_cast<ListBox*>(library.getChildComponent(1));
    auto* search = dynamic_cast<TextEditor*>(library.getChildComponent(0));
    require(list != nullptr && search != nullptr, "Library controls missing");
    search->setText("Preserve my search");
    Colour darkCanvas;
    const auto contrast = [](Colour a, Colour b)
    {
        const auto luminance = [](Colour colour)
        {
            const auto linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * linear(colour.getFloatRed()) + 0.7152f * linear(colour.getFloatGreen())
                + 0.0722f * linear(colour.getFloatBlue());
        };
        const float x = luminance(a), y = luminance(b);
        return (jmax(x, y) + 0.05f) / (jmin(x, y) + 0.05f);
    };
    for (bool dark : { true, false, true })
    {
        theme.applyDarkMode(dark);
        require(HostDesign::isDarkMode() == dark, "Appearance mode did not change");
        require((HostDesign::canvas.getPerceivedBrightness() < 0.3f) == dark,
                "Canvas does not match selected appearance");
        require(list->findColour(ListBox::backgroundColourId) == HostDesign::canvas
                && search->findColour(TextEditor::backgroundColourId) == HostDesign::control
                && search->findColour(TextEditor::textColourId) == HostDesign::text,
                "Existing library controls retained the previous palette");
        require(search->getText() == "Preserve my search", "Appearance change reset the search");
        require(contrast(HostDesign::text, HostDesign::canvas) >= 7.0f
                && contrast(HostDesign::secondary, HostDesign::surface) >= 4.5f,
                "Appearance text has insufficient contrast");
        if (dark) darkCanvas = HostDesign::canvas;
        else require(HostDesign::canvas != darkCanvas, "Palette references did not update");
    }
    library.removeFromDesktop();
    library.setLookAndFeel(nullptr);
    theme.applyDarkMode(previous);
}

void checkPopupTransparency()
{
    LightHostLookAndFeel theme;
    Image menuImage(Image::ARGB, 280, 180, true);
    Graphics menuGraphics(menuImage);
    theme.drawPopupMenuBackground(menuGraphics, menuImage.getWidth(), menuImage.getHeight());
    for (const auto& image : { menuImage })
    {
        for (const auto point : { Point<int>(0, 0), Point<int>(279, 0), Point<int>(0, 179), Point<int>(279, 179) })
            require(image.getPixelAt(point.x, point.y).getAlpha() == 0,
                    "A popup's outer corner was painted instead of remaining transparent");
        require(image.getPixelAt(11, 11).getAlpha() < 255
                && image.getPixelAt(140, 90).isOpaque(),
                "Popup rounding or its opaque interior was lost");
        const int shadowAlpha = image.getPixelAt(140, 173).getAlpha();
        require(shadowAlpha > 0 && shadowAlpha < 100,
                "Popup shadow was missing or became an opaque backing rectangle");
    }
    const int leftGutterAlpha = menuImage.getPixelAt(4, 22).getAlpha();
    const int rightGutterAlpha = menuImage.getPixelAt(275, 22).getAlpha();
    theme.drawPopupMenuItem(menuGraphics, juce::Rectangle<int>(0, 12, 280, HostDesign::menuRowHeight),
                           false, true, true, false, true, "Sort by", String(), nullptr, nullptr);
    require(menuImage.getPixelAt(4, 22).getAlpha() == leftGutterAlpha
            && menuImage.getPixelAt(275, 22).getAlpha() == rightGutterAlpha,
            "A highlighted menu row painted over the transparent shadow gutter");

#if JUCE_WINDOWS
    if (Desktop::canUseSemiTransparentWindows())
    {
        auto& desktop = Desktop::getInstance();
        const int initialWindows = desktop.getNumComponents();
        PopupMenu menu;
        menu.setLookAndFeel(&theme);
        menu.addItem(1, "Rename");
        menu.addColouredItem(2, "Remove", HostDesign::danger);
        menu.addItem(3, "Unavailable", false);
        menu.addSeparator();
        menu.addItem(4, "Close");
        menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(200, 200, 1, 1)),
                          ModalCallbackFunction::create([] (int) {}));
        require(desktop.getNumComponents() == initialWindows + 1,
                "Popup menu created additional backing or shadow windows");
        Component* popup = nullptr;
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (desktop.getComponent(i)->getName() == "menu") popup = desktop.getComponent(i);
        require(popup != nullptr && !popup->isOpaque() && popup->getPeer() != nullptr
                && (popup->getPeer()->getStyleFlags() & ComponentPeer::windowHasDropShadow) == 0,
                "Menu retained the legacy rectangular native shadow");
        require(popup->getNumChildComponents() > 0
                && popup->getChildComponent(0)->getY() >= HostDesign::popupShadowMargin + 2,
                "Menu items overlap the rounded panel's shadow margin");
        require(theme.getMouseCursorFor(*popup->getChildComponent(0)) == MouseCursor::PointingHandCursor
                && theme.getMouseCursorFor(*popup->getChildComponent(2)) == MouseCursor::NormalCursor
                && theme.getMouseCursorFor(*popup->getChildComponent(3)) == MouseCursor::NormalCursor,
                "Popup cursor does not distinguish actions from disabled rows and separators");
        PopupMenu::dismissAllActiveMenus();
        MessageManager::getInstance()->runDispatchLoopUntil(20);
        require(desktop.getNumComponents() == initialWindows,
                "Dismissing the popup left a native backing window behind");

        for (bool initiallyEnabled : { false, true })
        {
            bool enabled = initiallyEnabled;
            int result = 0;
            PopupMenu startupMenu;
            startupMenu.setLookAndFeel(&theme);
            auto* item = new HostDesign::SwitchMenuItem("Run on startup", enabled);
            startupMenu.addCustomItem(3, item);
            auto* toggle = dynamic_cast<HostDesign::Switch*>(item->getChildComponent(0));
            require(toggle != nullptr && toggle->getToggleState() == enabled,
                    "Tray startup item did not use the Settings switch with the current state");
            startupMenu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(200, 200, 1, 1)),
                ModalCallbackFunction::create([&](int id) { result = id; if (id == 3) enabled = !enabled; }));
            popup = nullptr;
            for (int i = 0; i < desktop.getNumComponents(); ++i)
                if (desktop.getComponent(i)->getName() == "menu") popup = desktop.getComponent(i);
            require(popup != nullptr, "Startup switch menu did not open");
            popup->keyPressed(KeyPress(KeyPress::downKey));
            popup->keyPressed(KeyPress(KeyPress::returnKey));
            MessageManager::getInstance()->runDispatchLoopUntil(20);
            require(result == 3 && enabled != initiallyEnabled,
                    "Selecting the startup switch did not invoke its action once");
            require(desktop.getNumComponents() == initialWindows,
                    "Selecting the startup switch left its menu window open");
        }
    }
#endif
}

void checkImportedBackupRestart()
{
    const File parent = File::getSpecialLocation(File::tempDirectory);
    const File root = parent.getNonexistentChildFile("LightHost-import-restart", "", false);
    require(root.isAChildOf(parent), "Invalid import restart fixture directory");
    const File oldApp = root.getChildFile("old-app"), newApp = root.getChildFile("new-app");
    const File original = oldApp.getChildFile("VST Plugins/import/Existing.vst3");
    const File restored = newApp.getChildFile("VST Plugins/import/Restored.dll");
    require(original.getParentDirectory().createDirectory().wasOk()
            && original.replaceWithText("existing binary")
            && restored.getParentDirectory().createDirectory().wasOk()
            && restored.replaceWithText("restored binary"), "Could not prepare import restart fixture");
    PluginDescription installed;
    installed.name = "Existing";
    installed.uid = 10;
    installed.pluginFormatName = "VST3";
    installed.fileOrIdentifier = original.getFullPathName();
    PluginDescription missing(installed);
    missing.name = "Restored";
    missing.uid = 11;
    missing.pluginFormatName = "VST";
    missing.fileOrIdentifier = oldApp.getChildFile("VST Plugins/import2/Restored.dll").getFullPathName();
    PluginDescription extracted(missing);
    extracted.fileOrIdentifier = restored.getFullPathName();
    KnownPluginList plugins;
    plugins.addType(installed);
    plugins.addType(missing);
    ScopedPointer<XmlElement> listXml(plugins.createXml());

    // Older archives may use legacy keys, and may or may not carry a portable root.
    for (bool legacy : { false, true })
        for (bool oldRootPresent : { false, true })
        {
            PropertySet settings;
            settings.setValue("pluginList", listXml);
            settings.setValue("pluginListActive", listXml);
            if (oldRootPresent) settings.setValue("portableAppDirectory", oldApp.getFullPathName());
            for (const auto& plugin : { installed, missing })
                for (const char* type : { "state", "bypass", "order", "display-name" })
                    settings.setValue(legacy ? PluginState::legacyKey(type, plugin) : PluginState::key(type, plugin),
                                      String(type) + " " + plugin.name);
            StringArray before, after;
            before.add(installed.fileOrIdentifier); after.add(installed.fileOrIdentifier);
            before.add(missing.fileOrIdentifier); after.add(extracted.fileOrIdentifier);
            PluginBackup::prepareImportedSettings(settings, before, after, newApp);
            ScopedPointer<XmlElement> saved(settings.createXml("PROPERTIES"));
            PropertySet restarted;
            restarted.restoreFromXml(*saved);
            PluginState::relocatePortableSettings(restarted, newApp);
            PluginState::migrateLegacySettings(restarted, { installed, extracted });
            for (const auto& plugin : { installed, extracted })
                for (const char* type : { "state", "bypass", "order", "display-name" })
                    require(restarted.getValue(PluginState::key(type, plugin)) == String(type) + " " + plugin.name,
                            "Restart after import lost plugin state, bypass, order, or display name");
            for (const char* key : { "pluginList", "pluginListActive" })
            {
                ScopedPointer<XmlElement> xml(XmlDocument::parse(restarted.getValue(key)));
                KnownPluginList reloaded;
                reloaded.recreateFromXml(*xml);
                require(reloaded.getNumTypes() == 2, "Import restart lost an effect");
                for (int i = 0; i < reloaded.getNumTypes(); ++i)
                    require(File(reloaded.getType(i)->fileOrIdentifier).existsAsFile(),
                            "Import restart rebased an existing or restored plugin to a missing file");
            }
            ScopedPointer<XmlElement> oldActive(XmlDocument::parse(restarted.getValue("pluginListActive")));
            PluginInstances upgraded;
            upgraded.recreateFromXml(*oldActive);
            upgraded.migrateLegacySettings(restarted);
            for (int i = 0; i < upgraded.getNumTypes(); ++i)
                for (const char* type : { "state", "bypass", "order", "display-name" })
                    require(restarted.getValue(upgraded.getType(i)->settingsKey(type))
                            == String(type) + " " + upgraded.getType(i)->name,
                            "An older imported backup lost settings when upgraded to instance identities");
            const File movedApp = root.getChildFile("moved-app");
            PluginState::relocatePortableSettings(restarted, movedApp);
            PluginDescription moved(extracted);
            moved.fileOrIdentifier = movedApp.getChildFile("VST Plugins/import/Restored.dll").getFullPathName();
            require(restarted.getValue(PluginState::key("state", installed)) == "state Existing"
                    && restarted.getValue(PluginState::key("state", moved)) == "state Restored",
                    "Moving an imported setup changed an external plugin path or lost portable state");
        }
    require(root.deleteRecursively(), "Could not remove import restart fixture");
}

void checkInstanceOrdering()
{
    PluginDescription plugin;
    PluginInstances instances;
    PropertySet settings;
    const auto first = instances.addType(plugin);
    const auto second = instances.addType(plugin);
    const auto third = instances.addType(plugin);
    const auto fourth = instances.addType(plugin);
    settings.setValue(first.settingsKey("order"), 5);
    settings.setValue(second.settingsKey("order"), -2);
    settings.setValue(third.settingsKey("order"), 5);
    auto ordered = instances.ordered(settings);
    require(ordered[0].instanceId == second.instanceId && ordered[1].instanceId == fourth.instanceId
            && ordered[2].instanceId == first.instanceId && ordered[3].instanceId == third.instanceId,
            "Ordering must preserve defaults, negative values, and insertion order for ties");
    settings.setValue(third.settingsKey("order"), -3);
    ordered = instances.ordered(settings);
    require(ordered[0].instanceId == third.instanceId && instances.getType(0)->instanceId == first.instanceId,
            "Ordering ignored changed settings or changed the underlying instance list");
}

void checkInstanceMigrationAndIsolation()
{
    PluginDescription plugin;
    plugin.name = "Loud Max";
    plugin.version = "1.0";
    plugin.pluginFormatName = "VST3";
    plugin.fileOrIdentifier = "/plugins/LoudMax.vst3";
    plugin.uid = 91;
    KnownPluginList oldList;
    oldList.addType(plugin);
    ScopedPointer<XmlElement> oldXml(oldList.createXml());
    for (bool legacy : { false, true })
    {
        PropertySet settings;
        for (const char* type : { "state", "bypass", "order", "display-name" })
            settings.setValue(legacy ? PluginState::legacyKey(type, plugin) : PluginState::key(type, plugin),
                              String(type) + " original");
        PluginInstances instances;
        instances.recreateFromXml(*oldXml);
        PluginState::migrateLegacySettings(settings, PluginState::collectPlugins(oldList, instances));
        instances.migrateLegacySettings(settings);
        const PluginInstance first = *instances.getType(0);
        require(PluginInstance::validId(first.instanceId), "Legacy chain entry did not receive an instance identity");
        for (const char* type : { "state", "bypass", "order", "display-name" })
            require(settings.getValue(first.settingsKey(type)) == String(type) + " original",
                    "Instance migration lost saved settings");

        const auto second = instances.addInstance(plugin, settings);
        require(second.instanceId != first.instanceId && second.displayName(settings) == "Loud Max 2"
                && !settings.containsKey(second.settingsKey("state"))
                && !settings.getBoolValue(second.settingsKey("bypass")),
                "A second instance shared the first instance's identity or settings");
        settings.setValue(second.settingsKey("state"), "second configuration");
        settings.setValue(second.settingsKey("bypass"), true);
        settings.setValue(first.settingsKey("display-name"), "Input limiter");
        require(first.displayName(settings) == "Input limiter" && second.displayName(settings) == "Loud Max 2",
                "Renaming one copy changed the other copy's display name");
        instances.move(1, 0, settings);
        require(instances.ordered(settings)[0].instanceId == second.instanceId
                && settings.getValue(first.settingsKey("state")) == "state original",
                "Reordering duplicate effects changed their identities or configurations");
        ScopedPointer<XmlElement> saved(instances.createXml());
        PluginInstances restarted;
        restarted.recreateFromXml(*saved);
        restarted.migrateLegacySettings(settings);
        require(restarted.getNumTypes() == 2 && restarted.ordered(settings)[0].instanceId == second.instanceId
                && settings.getValue(second.settingsKey("state")) == "second configuration"
                && settings.getBoolValue(second.settingsKey("bypass")),
                "Restart collapsed duplicate effects or lost independent settings");
        require(restarted.removeInstance(first.instanceId, settings)
                && restarted.getNumTypes() == 1
                && restarted.getType(0)->instanceId == second.instanceId
                && !settings.containsKey(first.settingsKey("state"))
                && settings.getValue(second.settingsKey("state")) == "second configuration",
                "Removing one copy removed or changed the other copy");
        const auto third = restarted.addInstance(plugin, settings);
        require(third.displayName(settings) == "Loud Max 3", "An added copy reused another instance's display name");
        saved = restarted.createXml();
        PluginInstances again;
        again.recreateFromXml(*saved);
        again.migrateLegacySettings(settings);
        require(!settings.containsKey(third.settingsKey("state")),
                "Restart seeded a fresh instance from obsolete binary-based settings");
    }
}

class SnapshotGain : public GainProcessor
{
public:
    explicit SnapshotGain(float value) : GainProcessor(value), setting(value) {}
    void processBlock(AudioBuffer<float>& audio, MidiBuffer&) override { ++calls; audio.applyGain(setting); }
    void getStateInformation(MemoryBlock& state) override
    {
        MemoryOutputStream output(state, false);
        output.writeFloat(setting);
    }
    void setStateInformation(const void* data, int size) override
    {
        if (size == 4) { MemoryInputStream input(data, (size_t) size, false); setting = input.readFloat(); }
    }
private:
    float setting;
};

void checkDuplicateInstanceAudioAndBackup()
{
    const File parent = File::getSpecialLocation(File::tempDirectory);
    const File root = parent.getNonexistentChildFile("LightHost-current-backup", "", false);
    const File source = root.getChildFile("Downloaded plugins/Gain.vst3");
    require(root.isAChildOf(parent) && source.getParentDirectory().createDirectory().wasOk()
            && source.replaceWithText("plugin binary fixture"), "Could not create current backup fixture");
    PluginDescription plugin;
    plugin.name = "Gain";
    plugin.pluginFormatName = "VST3";
    plugin.fileOrIdentifier = source.getFullPathName();
    plugin.uid = 92;
    PropertySet settings;
    PluginInstances instances;
    const auto first = instances.addInstance(plugin, settings);
    const auto second = instances.addInstance(plugin, settings);
    settings.setValue(first.settingsKey("display-name"), "Quiet gain");
    settings.setValue("mainWindowState", "fs -1600 80 1250 820");
    settings.setValue("lastPluginImportPath", source.getParentDirectory().getFullPathName());
    settings.setValue("darkMode", false);
    XmlElement audioSetup("DEVICESETUP");
    audioSetup.setAttribute("audioDeviceRate", 48000);
    audioSetup.setAttribute("audioDeviceBufferSize", 256);
    settings.setValue("audioDeviceState", &audioSetup);
    AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000, 64);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);
    auto* firstDsp = new SnapshotGain(0.5f);
    auto* secondDsp = new SnapshotGain(0.25f);
    graph.addNode(firstDsp, 41);
    graph.addNode(secondDsp, 42);
    auto reconnect = [&]
    {
        const auto list = instances.ordered(settings);
        require(PluginChain::reconnect(graph, (int) list.size(),
            [&](int i) { return settings.getBoolValue(list[i].settingsKey("bypass")); },
            [&](int i) { return list[i].instanceId == first.instanceId ? uint32(41) : uint32(42); }).isEmpty(),
            "Could not connect duplicate-instance fixture");
    };
    reconnect(); checkAudio(graph, 0.03125f);
    settings.setValue(second.settingsKey("bypass"), true);
    const int secondCalls = secondDsp->calls;
    reconnect(); checkAudio(graph, 0.125f);
    require(secondDsp->calls == secondCalls && !firstDsp->isSuspended(),
            "Bypassing the second copy processed it or suspended the first copy");
    settings.setValue(second.settingsKey("bypass"), false);
    instances.move(1, 0, settings);
    reconnect(); checkAudio(graph, 0.03125f);
    require(graph.getNodeForId(41)->getProcessor() == firstDsp && graph.getNodeForId(42)->getProcessor() == secondDsp,
            "Reordering duplicate effects replaced their processors");
    for (int i = 0; i < 2; ++i)
    {
        MemoryBlock state;
        PluginState::capture(graph, i == 0 ? *firstDsp : *secondDsp, state);
        settings.setValue((i == 0 ? first : second).settingsKey("state"), state.toBase64Encoding());
    }
    require(settings.getValue(first.settingsKey("state")) != settings.getValue(second.settingsKey("state")),
            "Duplicate processors did not retain different parameter states");
    settings.setValue(second.settingsKey("bypass"), true);
    ScopedPointer<XmlElement> active(instances.createXml());
    settings.setValue("pluginListActive", active);
    KnownPluginList known;
    known.addType(plugin);
    ScopedPointer<XmlElement> available(known.createXml());
    settings.setValue("pluginList", available);
    require(PluginState::collectPlugins(known, instances).size() == 1,
            "Backup included the same plugin binary more than once");
    require(PluginBackup::hasValidPluginLists(settings, true), "Valid multi-instance backup rejected");

    {
        // Exercise the production exporter, extraction, settings save, and startup migration.
        ScopedPointer<XmlElement> xml(settings.createXml("PROPERTIES"));
        XmlElement manifest("LIGHTHOST_BACKUP");
        manifest.setAttribute("version", 3);
        auto* binary = manifest.createNewChildElement("PLUGIN");
        binary->setAttribute("id", 0);
        binary->setAttribute("format", plugin.pluginFormatName);
        binary->setAttribute("source", plugin.fileOrIdentifier);
        binary->setAttribute("archiveRoot", "plugins/0");
        binary->setAttribute("isDirectory", false);
        const File backup = root.getChildFile("current.lightbak");
        PluginBackup::ExportTask worker(backup);
        worker.plugins.add(source);
        worker.settingsText = xml->createDocument(String(), true);
        worker.manifestText = manifest.createDocument(String(), true);
        worker.startThread(3);
        require(worker.waitForThreadToExit(10000) && worker.succeeded,
                "Production exporter failed to write the current backup");
        require(source.deleteFile(), "Could not remove original binary before restore");
        ZipFile archive(backup);
        require(archive.getNumEntries() == 3, "Backup did not contain one binary, settings, and manifest");
        ScopedPointer<InputStream> manifestStream(archive.createStreamForEntry(archive.getIndexOfFileName("manifest.xml")));
        require(manifestStream != nullptr, "Backup omitted its manifest");
        ScopedPointer<XmlElement> importedManifest(XmlDocument::parse(manifestStream->readEntireStreamAsString()));
        require(importedManifest != nullptr && importedManifest->hasTagName("LIGHTHOST_BACKUP")
                && PluginBackup::supportsVersion(importedManifest->getIntAttribute("version")),
                "Current backup manifest was not compatible with import");
        ScopedPointer<InputStream> restoredStream(archive.createStreamForEntry(archive.getIndexOfFileName("settings.xml")));
        require(restoredStream != nullptr, "Backup omitted its settings");
        ScopedPointer<XmlElement> restoredXml(XmlDocument::parse(restoredStream->readEntireStreamAsString()));
        require(restoredXml != nullptr, "Backup settings could not be read");
        PropertySet imported;
        imported.restoreFromXml(*restoredXml);
        require(PluginBackup::hasValidPluginLists(imported, true), "Exported instance lists were invalid");
        const File destination = root.getChildFile("restored-app");
        const File restoredBinary = PluginBackup::restoreDestination(source, FileSearchPath(), destination.getChildFile("VST Plugins"));
        const int binaryIndex = archive.getIndexOfFileName("plugins/0/Gain.vst3");
        require(binaryIndex >= 0 && restoredBinary.getParentDirectory().createDirectory().wasOk(),
                "Backup omitted the downloaded plugin binary or its restore folder");
        {
            ScopedPointer<InputStream> stream(archive.createStreamForEntry(binaryIndex));
            FileOutputStream output(restoredBinary);
            require(stream != nullptr && !output.failedToOpen()
                    && output.writeFromInputStream(*stream, -1) == archive.getEntry(binaryIndex)->uncompressedSize,
                    "Could not restore the downloaded plugin binary");
            output.flush();
            require(output.getStatus().wasOk(), "Restored binary could not be saved");
        }
        require(restoredBinary.loadFileAsString() == "plugin binary fixture", "Backup changed the plugin binary");
        StringArray oldPaths, newPaths;
        oldPaths.add(plugin.fileOrIdentifier); newPaths.add(restoredBinary.getFullPathName());
        PluginBackup::prepareImportedSettings(imported, oldPaths, newPaths, destination);
        PropertiesFile::Options options;
        options.millisecondsBeforeSaving = -1;
        const File settingsFile = destination.getChildFile("Data/Light Host.settings");
        {
            PropertiesFile saved(settingsFile, options);
            saved.addAllPropertiesFrom(imported);
            require(saved.saveIfNeeded(), "Imported settings could not be saved");
        }
        PropertiesFile restarted(settingsFile, options);
        require(restarted.isValidFile(), "Imported settings could not be loaded after restart");
        PluginState::relocatePortableSettings(restarted, destination);
        require(restarted.getValue("mainWindowState") == settings.getValue("mainWindowState")
                && restarted.getValue("lastPluginImportPath") == settings.getValue("lastPluginImportPath")
                && !restarted.getBoolValue("darkMode", true)
                && restarted.getValue("audioDeviceState") == settings.getValue("audioDeviceState"),
                "Backup restart lost window placement, picker folder, appearance, or audio settings");
        ScopedPointer<XmlElement> importedList(XmlDocument::parse(restarted.getValue("pluginListActive")));
        PluginInstances restored;
        restored.recreateFromXml(*importedList);
        restored.migrateLegacySettings(restarted);
        require(restored.getNumTypes() == 2 && restored.ordered(restarted)[0].instanceId == second.instanceId,
                "Backup restore collapsed instances or lost their order");
        for (const auto& entry : restored.ordered(restarted))
        {
            require(entry.fileOrIdentifier == restoredBinary.getFullPathName(), "Restored instance did not follow its binary");
            require(entry.displayName(restarted) == (entry.instanceId == first.instanceId ? "Quiet gain" : "Gain 2")
                    && restarted.getBoolValue(entry.settingsKey("bypass")) == settings.getBoolValue(entry.settingsKey("bypass")),
                    "Backup restart lost an independent instance name or bypass state");
            MemoryBlock state;
            state.fromBase64Encoding(restarted.getValue(entry.settingsKey("state")));
            SnapshotGain processor(1.0f);
            processor.setStateInformation(state.getData(), (int) state.getSize());
            AudioBuffer<float> audio(2, 1);
            audio.setSample(0, 0, 1); audio.setSample(1, 0, 1);
            MidiBuffer midi;
            processor.processBlock(audio, midi);
            require(audio.getSample(0, 0) == (entry.instanceId == first.instanceId ? 0.5f : 0.25f),
                    "Restored copies shared or lost their parameter configurations");
        }
        importedList->getChildElement(1)->setAttribute("instanceId", importedList->getChildElement(0)->getStringAttribute("instanceId"));
        imported.setValue("pluginListActive", importedList);
        require(!PluginBackup::hasValidPluginLists(imported, true), "Backup accepted shared instance identities");
        importedList->getChildElement(1)->removeAttribute("instanceId");
        imported.setValue("pluginListActive", importedList);
        require(!PluginBackup::hasValidPluginLists(imported, true), "Version 3 backup accepted a missing identity");
    }
    settings.setValue(second.settingsKey("bypass"), false);
    require(instances.removeInstance(first.instanceId, settings), "Could not remove the first processor instance");
    graph.removeNode(41);
    reconnect(); checkAudio(graph, 0.0625f);
    require(graph.getNodeForId(42)->getProcessor() == secondDsp,
            "Removing one copy replaced the surviving copy's processor");
    require(root.isAChildOf(parent) && root.deleteRecursively(), "Could not remove current backup fixture");
}
}

int main()
{
    ScopedJuceInitialiser_GUI juceInitialiser;
    try
    {
        checkChain();
        checkStableNodeRouting();
        checkChannelSetup();
        checkMinimizedAudioTiming();
        checkAudioDenormals();
        checkPlayerDiagnostics();
        checkIdentityAndMigration();
        checkBackups();
        checkUnsupportedLayoutAndEditor();
        checkSynchronizedStateCapture();
        checkPluginLibraryActions();
        checkBypassPresentation();
        checkHostCursors();
        checkAppearanceChanges();
        checkPopupTransparency();
        checkImportedBackupRestart();
        checkInstanceOrdering();
        checkInstanceMigrationAndIsolation();
        checkDuplicateInstanceAudioAndBackup();
        std::cout << "PASS: stereo routing, bypass CPU work, resume, instance preservation, missing plugins\n";
        std::cout << "PASS: stable node IDs, reorder/add/remove DSP preservation, selective editor cleanup, background backup and cancellation\n";
        std::cout << "PASS: stereo channel migration, invalid-output containment, device diagnostics\n";
        std::cout << "PASS: denormal input/tail protection, driver float-mode restoration, callback gap diagnostics\n";
        std::cout << "PASS: plugin identity migration, backup round trips, active-only backups, editor reuse, unsupported layouts, state synchronization\n";
        std::cout << "PASS: inline library metadata and search, filtered removal preserving active copies, independent adds, stale results, selection clearing\n";
        std::cout << "PASS: tooltip-free library, shared startup switches with keyboard activation, transparent menus and dismissal cleanup\n";
        std::cout << "PASS: live dark/light switching, existing library colors, preserved search, and text contrast\n";
        std::cout << "PASS: clickable button, switch, selector, and menu cursors; disabled, text, and resize cursors preserved\n";
        std::cout << "PASS: bypass indication for loaded and missing effects; legacy and current backup imports survive restart and later moves\n";
        std::cout << "PASS: independent instance migration, fresh defaults, rename/order/removal isolation, duplicate DSP bypass, version 3 backup configurations\n";
        std::cout << "PASS: production backup export, downloaded binary restore, and disk restart preserve window placement, picker folder, appearance, audio, and independent instance settings\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
