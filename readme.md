Light Host
---

A simple VST/AU host for OS X, Windows, and Linux that sits in the menu/task bar.

### Features

See [#1](https://github.com/rolandoislas/LightHost/issues/1)

### Interface

The Windows interface uses a native title bar, Segoe UI, and an Apple-inspired dark theme. Switch between dark and light in **Settings > App > Dark mode**; the change applies immediately and is saved for the next launch. **Signal chain** shows effects in processing order between the selected input and output devices. Drag an effect by its grip to reorder it, use its power control to bypass or enable it, and double-click it or press Enter to open its editor. The ellipsis menu contains rename and remove actions. Unavailable effects are marked in the chain.

The host uses one shared theme for library, plugin, tray, and device menus, with rounded surfaces, consistent row spacing, keyboard selection, and red destructive actions. Scrollbars share the same slim rounded thumb across the chain, library, settings, and dialogs. Fields, buttons, and progress indicators use the same typography, colors, and focus treatment. Host tooltips are disabled.

The adjacent **Plugin library** shows each effect's manufacturer, format, category, and version directly in its row. Search matches those details, names, and descriptions. Use an effect's **+** button or select it and press Enter to add an independent instance. You can add the same effect repeatedly; additional copies start with the plugin's defaults and receive names such as **LoudMax 2**. Each copy has its own parameters, bypass state, display name, editor, and chain position. The library shows how many copies are in the chain. **Scan for plugins...** starts the scanner directly, and **Library options...** provides sorting, removal, folder reveal, and retrying skipped plugins in the same panel. You can also drop plugin files into the library. Selecting a library row and pressing **Delete** removes its library entry while retaining active copies and installed files. **Ctrl+F** focuses library search. With a chain row focused, **Space** toggles bypass, **F2** renames, **Delete** removes, and **Ctrl+Up/Down** reorders it.

**Add plugin** opens a native file picker where you can select multiple VST plugin files (`.dll` or `.vst3` on Windows) to import into the library. The picker starts in Downloads (or Documents if Downloads is unavailable), then remembers the last folder. If that folder no longer exists, it returns to the default. Imported plugins stay at their original locations; use their library **+** buttons to add them to the signal chain. Files that cannot be loaded are reported after import.

Effect cards show **Active**, **Bypassed**, or **Unavailable**. A missing effect still shows its saved bypass state; its power control changes between amber (enabled but unavailable) and gray (bypassed). The more compact cards and explicit Windows font weights keep longer chains readable with less scrolling.

**Settings** uses a sliders icon and groups audio configuration, startup and tray preferences, backup actions, and saved plugin state reset. **Run on startup** uses the same switch in Settings and the right-click tray menu; both reflect the current Windows startup registration. The sidebar reports the current audio device's running state, sample rate, and buffer size. Closing the main window keeps the host running in the system tray; use the tray menu to quit. The main window remembers its size, screen position, and maximized state across restarts. If its saved monitor is unavailable, the window is brought back onto an available screen.

### Backup and restore

Configuration and audio logs are stored in the `Data` folder beside the executable, regardless of the working directory or shortcut used to launch it. Existing settings and logs beside the executable move there automatically. AppData and previous executable locations are never searched for settings. Backup dialogs also default to the executable's directory. To preserve your setup when moving the app, move the executable, `Data`, and `VST Plugins` together into a writable directory. Moving only the executable uses the destination folder's configuration, or creates fresh settings there if none exist. Plugin paths inside the app folder and their saved states follow the new location automatically; externally installed plugins retain their existing paths.

Open Settings from the Light Host tray window and choose **Export backup...** to create a `.lightbak` file containing your Light Host settings, plugin list, active plugin states, audio device settings, and discovered file-based plugin binaries. Choose **Import backup...** to restore the settings and missing plugin files into the `VST Plugins` folder beside the executable. Light Host automatically reloads after a successful import to apply the restored settings. Import accepts only `.lightbak` files, which use ZIP compression internally.

Backups also include saved window size, position, and maximized state, the last plugin import folder, and app appearance preferences. These use the existing settings payload, so they do not require a new backup format version. Older backups without these preferences use the app defaults.

Existing plugin files are never overwritten. Plugin installers, license data, external dependencies, and plugins managed by the operating system are not included; those may need to be installed or authorized separately. Backups can be large, and plugin binaries may be subject to their authors' license terms.

Import resets the backup's portable app location after resolving plugin paths. This prevents a restart from moving references to an existing installation into a folder where the plugin files do not exist. Original version 1 and version 2 backups remain supported, with saved effect states, names, order, and bypass settings preserved.

### Windows build

When **Run on startup** is enabled, launching Light Host refreshes its Windows startup entry to the current executable location. After moving or renaming the executable, launch it once from the new location before signing out. Turning startup off removes the entry; subsequent launches leave it off. If you keep multiple copies, the last copy launched becomes the startup target.

To build the 64-bit Windows executable locally:

1. Install Git for Windows and Visual Studio 2022 with the **Desktop development with C++** workload and **C++ CMake tools** component.
2. From the repository root, run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
   ```

The script downloads the pinned JUCE and Steinberg VST3 SDK revisions into `lib/` if they are not already present, builds Projucer, generates the Visual Studio project, and builds the application in Release mode. The executable is written to `Builds/Windows/Release/Light Host.exe`. To build Debug instead, add `-Configuration Debug` to the command.

To build alongside a running executable, use a separate destination, for example `-OutputPath Builds\Windows\UpdatedRelease`.

The build retains VST3 hosting. ASIO is enabled when the Steinberg ASIO SDK is found at `lib/ASIOSDK`, `C:\Dev\SDKs\ASIOSDK`, or the location specified by `ASIO_SDK_PATH`. You can also pass `-AsioSdkPath C:\path\to\ASIOSDK`. If the SDK is unavailable, the script builds without ASIO and prints a warning. Obtain the SDK from Steinberg and follow its license terms. Generated build output, JUCE project files, and downloaded dependencies are ignored by Git.

Add `-RunTests` to build and run the audio routing regression checks. These use in-memory stereo buffers without opening an audio device. They verify empty-chain passthrough, chained processing, bypass/resume, preservation of plugin instances, and missing-plugin handling. They also verify that bypassed plugins receive no processing calls. Test binaries are written to `Builds/Tests/Release/` (or `Debug/`).

### Audio stability

Light Host processes up to two selected input and output channels. Older settings that enabled more channels retain their first two selected channels; sample rate and buffer size are preserved. The Windows build applies the tracked JUCE ASIO patch before generating the project, so repeated driver resynchronization requests cannot keep postponing a pending reset.

On Windows 11, minimized or covered windows can cause Windows to ignore a process's high-resolution timer requests. This can delay an ASIO driver's waits and produce robotic audio even when plugin processing finishes on time. Light Host disables that timer throttling while its audio player exists, including when minimized or closed to the tray, and restores the previous process policy on shutdown. The API is loaded at runtime so older Windows versions remain supported. The status display stops polling while minimized. A native Windows regression check verifies timer protection, unchanged processing across window states, and restoration of the previous policy. See [Microsoft's timer resolution documentation](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod).

Use **Settings → Audio → Restart audio** to reopen the device and recreate the plugin chain while retaining plugin settings. This briefly interrupts audio and closes plugin editors. **Reset device** in the device selector only reopens the device and reprepares the existing plugins.

Audio diagnostics are saved as `Data/Light Host.audio.log` beside the executable. They summarize stream settings, device errors, callbacks that exceed the buffer duration, gaps between callbacks exceeding twice the buffer duration, and buffers containing NaN/infinite output. The gap counter measures idle time after one callback finishes and before the next starts; it can reveal driver or scheduling stalls even when plugin processing finishes on time. It is a diagnostic hint, not a driver underrun count. Invalid samples are silenced. Diagnostic writes run outside the audio callback, are batched every five seconds, and the log is trimmed to approximately 128 KiB. Diagnostics do not record microphone audio. They cannot detect every driver underrun or distortion consisting of otherwise finite samples.

On Intel/AMD processors, the audio callback enables denormals-are-zero and flush-to-zero while plugins process audio, then restores the driver thread's previous floating-point mode. This prevents extremely quiet filter/reverb tails from causing subnormal arithmetic slowdowns that can interrupt audio. Keep this protection around plugin processing; checking the final output cannot prevent the earlier CPU stall.

The regression checks also cover channel migration, invalid-output containment, denormal inputs and decaying tails, driver floating-point mode restoration, processing overruns, callback gaps, and propagation of rate/buffer changes when a device restarts. Physical ASIO recovery and third-party plugin behavior still require testing with the actual device and plugins.

Each active effect has a persistent instance identity for its state, order, bypass, and display name. Existing settings are migrated automatically. Version 3 backups preserve independent copies of the same effect, include each plugin binary once, and retain single-file versus bundle layouts. Version 1 and version 2 backups remain supported, including original backups without a layout flag. Older app versions cannot restore version 3 backups. Regression checks exercise the production backup exporter, restore a downloaded plugin fixture with its original removed, and reload imported settings from disk to verify independent effect states and current app preferences.

Only effects with at least two input and two output channels are supported in the stereo chain. Other layouts are skipped with a warning, and the remaining chain continues passing audio. Plugin state snapshots synchronize with processing in graph-then-processor lock order; a plugin with a slow state callback can briefly interrupt audio during saving.

### Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)
