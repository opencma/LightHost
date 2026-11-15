Light Host
---

A simple VST/AU host for OS X, Windows, and Linux that sits in the menu/task bar.

### Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)

Below is AI generated:
---

All builds support VST2 and VST3 effects. On Windows, use 64-bit
plugins with the 64-bit build.

macOS and Linux are untested; they **should** work, but platform builds and
plugin compatibility have not yet been verified.

### Features

See [#1](https://github.com/rolandoislas/LightHost/issues/1)

The audio graph supports up to eight channels in either **Stereo pair** mode or
**Independent channels** mode (select the routing mode in the main window).
Stereo pair mode processes one pair; a mono input is copied to both inputs of a
stereo effect. Independent channels mode creates a separate plugin chain for
each matching input/output channel, without mixing channels together. It
processes at most the first eight matching channels; channels without a matching
input or output are not processed. Stereo-only plugins receive the individual
channel on both inputs, and only their first output channel is used. This is
per-channel routing, not a native multibus surround layout. The same active
plugin list is shared across channels. Select a plugin and use **Plugin
channels...** to choose which channels use that plugin; different channels can
therefore use different subsets of the active list. Channel assignments are
saved with the active plugin list.

### Voicemeeter

On Windows, to insert effects into a Voicemeeter input strip, use
**Voicemeeter Virtual Insert ASIO** rather than a Windows playback or recording
endpoint:

1. In Voicemeeter select your normal A1 hardware output and input source. Turn
   down monitoring while setting up, and check the A1 sample rate and buffer.
2. In Light Host's **Preferences > Audio Settings**, choose **ASIO** and
   **Voicemeeter Virtual Insert ASIO**. If another app is using the insert driver,
   close or disconnect it first; the driver supports one client.
3. In **Audio Settings**, select the input and output channels belonging to the
   desired strip, using the driver's channel labels. Select matching channels
   on both sides. Choose **Stereo pair** in the main window to process a pair
   together, or **Independent channels** to run separate plugin instances on
   each matching channel. The independent mode is capped at eight channels;
   it does not automatically follow Voicemeeter's insert buttons.
4. In Voicemeeter's **Menu > System Settings / Options > Patch Insert**, enable
   the left and right insert returns for that strip. The small `i>` indicator on
   the strip also controls its insert return. The signal returns to that strip
   before its fader and its A1/A2/A3/B1/B2 routing.
5. Add effects in Light Host and check the signal at low volume. If the insert is
   silent, turn off its return in Voicemeeter while troubleshooting. Avoid routing
   a Voicemeeter output back into one of its inputs, which can create feedback.

The insert driver uses Voicemeeter A1's sample rate and buffer. For other Windows
audio devices, if input and output have no common sample rate, check each device's
**Windows Sound > More sound settings > Properties > Advanced** format and try
matching them (for example 48000 Hz), then select that rate in Light Host.
This is one possible cause of an audio-device error, not the only one.

If CPU-heavy effects crackle, try a larger buffer in **Audio Settings** or the
audio driver's control panel. A larger buffer can reduce dropouts but adds
latency; available sizes depend on the selected device and driver.
The main window shows the total plugin-reported latency in samples and
milliseconds. This does not include latency added by the audio device, driver,
or other applications in the signal path. High-latency plugins (including
linear-phase filters and large FFT effects) may add substantial delay; reduce
their internal quality/FFT settings when low monitoring latency is important.

### Building (Windows)

Requires Visual Studio 2022 with the "Desktop development with C++" workload and JUCE
(tested with 9.0.5). CMake is included with Visual Studio. JUCE is not bundled;
`lib/` is ignored by Git. From the repository root, install the tested version with:

```
git clone --depth 1 --branch 9.0.5 https://github.com/juce-framework/JUCE.git lib/juice
```

The build looks for JUCE in `lib/juice`, `lib/juce`, or `LightHost/lib/juice`;
otherwise pass `-DJUCE_DIR=<path to JUCE>`.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target LightHost
```

The app is written to `build\LightHost_artefacts\Release\Light Host.exe`.
`LightHost.jucer` is the legacy JUCE 4 project and is no longer used for building.
The main Light Host window stays open while you manage plugins. Closing it hides
the window; reopen it from the tray menu, or quit from the tray icon.

### VST2 plugins

> #### NOTE from human:
> I had the AI build and create a VST2.0 sdk. If there are issues, please let
> me know with as much information/logs as possible. If you need help with this
> I can guide you, just make sure to add these to an issue.

Use **Edit Plugins** to scan the folder containing your VST2 `.dll` files, then
add an effect from the plugin list. The bundled GPL-licensed compatibility
interface is in `VST2Compat`; no separate VST2 SDK is needed. You can point
`VST2_SDK_DIR` to another compatible SDK root. VST2 support is required; an
empty `VST2_SDK_DIR` uses the bundled interface. Check applicable licenses
and trademark requirements before distributing the app.
If scanning does not list a supported plugin, use **Add plugin file...** in the
main window to select its file or bundle directly. Light Host checks whether it
can identify and instantiate the plugin before adding it; if the file contains
multiple plugin types, choose which one to add.

### Saved plugins and settings

The active plugin list is processed from top to bottom. Use **Move up** and
**Move down** to change the order.
The same plugin can be added more than once; each instance has its own saved
state and bypass setting. Existing saved chains are migrated on first launch.

**Bypass shortcut...** configures a shortcut for the selected plugin. On
Windows and macOS it is registered with the operating system. On Linux it is
global in X11 sessions; Wayland does not provide this X11 app a portable global
shortcut registration API, so the shortcut falls back to working only while
the Light Host window is focused. Leave the field empty to disable it. The app
reports when the global combination cannot be registered because it is already
in use or the desktop does not support it.

If a saved plugin cannot load, Light Host reports the reason and keeps its saved
state. Reinstall missing plugins at their original locations, or scan for them
again in **Edit Plugins**. If a plugin has moved, use **Active Plugins > [plugin]
\> Locate plugin...** and choose its new file or VST3 bundle. Light Host checks
that it loads and matches the saved plugin before changing its location. A VST3
replacement for a VST2 plugin may not accept the old plugin's settings, so
keep a backup of your settings before changing it.
When replacing a VST2 entry with a VST3, Light Host can use default settings in
the same chain position after confirmation, keeping the old saved state. JUCE
cannot reliably report whether a plugin accepted a different format's state, so
this replacement does not automatically attempt to migrate it. Some plugins
may not restore their parameters as expected; keep a backup and verify settings
after restoring them.

On Windows, settings are stored in `%APPDATA%\Light Host\Light Host.settings`,
not next to the executable. To back up your setup, quit Light Host and copy this
file. To recover from a broken configuration, quit Light Host and rename the
file rather than deleting it; restore it later if needed. Plugin paths must also
exist on the destination machine.
Audio device type, device names, enabled channels, sample rate, and buffer size
are saved as they change. If a saved device is missing at startup, Light Host
tries the system default and saves that fallback. This does not guarantee
recovery from every audio-driver or Windows Audio service failure; reopen
**Audio Settings** or restart Light Host if the device does not recover.

To run a second Light Host process with separate settings, start it with
`-multi-instance=2`. Use a different non-empty suffix for each additional
instance. Audio drivers may still limit access to one process at a time.

This build does not currently write a log file.
Known audio and plugin load errors and exceptions caught by the JUCE message loop
show a diagnostic stack at the reporting point. This is not necessarily the
original failure location; release builds may lack symbols. Native crashes and
failures in plugin threads cannot be guaranteed to show a popup.

### Installer and updates

Light Host checks `https://annaspannertechsolutions.uk/lighthost/latest` at
startup; the tray menu also has **Check for updates**. It compares semantic
versions, downloads the platform-specific GitHub Release asset, verifies its
SHA-256 against the manifest, and asks before installing.

On Windows, build the installer after building the application:

```
cmake --build build --config Release --target LightHostSetup
```

The network installer is `build\Release\Light Host Setup.exe`. It downloads and
SHA-256-verifies the application from the update manifest before installing or
upgrading it in the current user's local application data. It offers optional
Start menu and desktop shortcuts. The updater runs this installer with the
current application directory, so an installation outside the default location
is updated in place. Existing plugin settings are not removed.

Windows releases require two manifest entries per architecture: `windows-x64`
or `windows-arm64` for the network installer, and `windows-x64-package` or
`windows-arm64-package` for the matching raw application executable. Publish
both GitHub Release assets and their exact hashes before deploying the manifest.

On Linux, the graphical network installer downloads the architecture-specific
ZIP package and installs the app and updater into `~/.local/share/Light Host`.
It offers application menu and desktop shortcuts. Downloads may need to be made
executable before launching (for example `chmod +x LightHostSetup-linux-x64`).
The updater verifies the ZIP, replaces both binaries, and restarts the app.
The install directory must be writable. The Linux build requires libcurl
development headers, and the installed app and installer require libcurl at
runtime for HTTPS.

On macOS, the release asset is a ZIP containing `Light Host.app`. The updater
replaces the app bundle and reopens it; the installed bundle location must be
writable by the current user. Unsigned or unnotarized app bundles may be blocked
by Gatekeeper. This project does not currently sign or notarize macOS builds. 

The macOS `.pkg` network installer downloads and verifies the app ZIP and
installs it in the logged-in user's `~/Applications`. It is built by the macOS
GitHub Actions jobs and can be signed and notarized when Apple credentials are
available. Installation may require administrator authorization. Close Light
Host before installing over an existing copy.

> *Human note:* Please raise an issue if you are interested in me getting this signed

The manifest must contain a URL and SHA-256 for the running architecture:
`windows-x64`, `windows-arm64`, `linux-x64-package`, `linux-arm64-package`,
`macos-x64`, or `macos-arm64`. Updates are not offered until a newer version
and matching platform asset are published.

The GitHub Actions Windows and Linux/macOS workflows produce architecture-specific
application packages, installers, and checksum files. Upload each installer and
its matching package as separate release assets. For Linux, the `-package`
manifest key points to the app ZIP; for macOS, the platform key points to the
app ZIP. Windows requires both its installer key and its `-package` app key as
described above.

Earlier Linux builds expect a standalone executable under `linux-x64` or
`linux-arm64`, not a ZIP. The new manifest deliberately omits those keys to
avoid breaking their updates. Users of those builds must run the new network
installer once; subsequent upgrades use the ZIP package automatically.

After collecting the installer and app artifacts in one folder, generate a
new manifest in PowerShell (do not deploy it until the release files are uploaded):

```powershell
.\Utilities\GenerateUpdateManifest.ps1 -AssetDirectory 'C:\path\to\release-assets' `
    -Version '1.2.2' -OutputPath 'C:\path\to\new-lighthost-manifest.json'
```

The script requires all six app/update assets and both Windows installers.
The Linux and macOS installer files are separate release downloads and are not
used by the update manifest.