# Light Host design concept

The original visual exploration was created with the built-in imagegen tool. The design is now implemented in the Windows desktop application with a native Windows title bar, Segoe UI, and the existing audio and backup workflows.

- [Signal chain](signal-chain-concept.png)
- [Settings](settings-concept.png)

Actual Windows screenshots:

- [Signal chain](windows-signal-chain.png)
- [Settings](windows-settings.png)
- [Multiple instances](multiple-instances-windows.png)
- [Integrated plugin library](integrated-library-windows.png)

These screenshots use an isolated copy of the saved configuration with the selected Voicemeeter ASIO device running. The original settings and saved effect states were preserved; six missing workspace plugin binaries were recovered from the matching original installation.

## Direction

A quiet native utility with the clarity and restraint of Apple's desktop apps: layered charcoal surfaces in dark mode, near-white surfaces in light mode, a softly separated sidebar, strong typography, fine separators, consistent outline icons, and a single blue accent. Color communicates selection and action; green indicates running audio, and red is reserved for destructive actions.

The signal chain is the primary workspace. Input and output endpoints frame the ordered effects. Each effect shows its name, format, stereo layout, power control, and action menu. A plugin library sits alongside the chain so discovery and adding an effect remain close to the task.

Settings group device configuration separately from app preferences, backup, and state deletion. Restarting audio is distinct from resetting the device, reflecting the existing behaviors.

## Feature coverage

| Existing capability | Proposed presentation |
| --- | --- |
| Ordered stereo processing | Vertical input-to-output chain with numbered effects |
| Drag to reorder | Grip on each effect row; visible insertion indicator during dragging |
| Individual bypass | Power control plus explicit Bypassed label; power on means processing enabled |
| Native or generic plugin editors | Double-click an effect to open its existing editor in a separate native window |
| Rename and remove an active effect | Ellipsis menu on each effect |
| Discover and add installed plugins | Adjacent library with add buttons and a scan action |
| Detailed library management | Inline maker, format, category, and version; sorting and management in Library options |
| Audio driver and device selection | Audio settings group with input and output devices |
| Stereo channel selection | Input and output channel controls limited to two each |
| Sample rate and buffer size | Dedicated preference rows |
| Output test and device reset | Secondary actions below device settings, shown when supported |
| Restart device and reload effects | Separate Restart audio action with interruption explained before execution |
| Startup registration | Run on startup switch on supported platforms |
| Tray icon appearance | Invert tray icon switch |
| Export and import .lightbak backups | Backup & restore group |
| Delete saved plugin states | Separate destructive action retaining the current reset behavior |
| Automatic settings and state saving | Quiet save status; only show confirmation after a successful save |
| Menu/task bar operation | Existing tray entry points retained; closing the main window keeps audio running |
| Version and credits | About view accessible in the sidebar |
| Audio diagnostics | Existing background log behavior retained; failures surface through actionable status |

## Proposed improvements

Library search, per-row add controls, visual signal connectors, and device-aware running status are implemented. Search matches names, manufacturers, formats, categories, versions, and descriptions. The library lists each binary once and can add it repeatedly. Every chain entry has an independent identity, configuration, bypass state, name, order, and editor. Each library row shows its maker, format, category, and version, with searchable metadata. Library options handles sorting, removal, folder reveal, and retrying skipped plugins directly in the panel. Scanning retains the folder picker and progress dialogs without opening a separate library window. Plugin files can be dropped into the panel; removing a library entry preserves active copies and installed files.

The original mockups use sample plugins and macOS device settings. The implementation lists the formats and controls available on the host platform; AU remains macOS-only. Startup registration is supported on Windows. Other platforms retain their native title bars and default fonts.

The readability revision explicitly resolves Segoe UI's Regular style instead of JUCE 4's unresolved default style, uses Semibold for smaller text, and increases secondary text contrast. Effect rows are now 68 pixels high instead of 88, fitting six effects at the default window size. Power colors and status text show bypass independently of whether an effect is currently available.

Host tooltips are removed, including the tray icon hint. Settings uses an outline sliders icon. The tray menu and Settings page share the same Run on startup switch; changes in the tray refresh an open Settings page.

The host theme is centralized in `Source/HostLookAndFeel.h`. Library options, nested menus, plugin actions, device selectors, and the tray menu use the same rounded popup rendering, 34-pixel rows, selection colors, disabled states, checkmarks, and destructive-action color. Plugin actions use standard popup items with keyboard support and lifetime-safe callbacks. All host scrolling containers use 10-pixel scrollbars with flat 6-pixel rounded thumbs, including settings and dialog lists. Dialog typography, input fields, and progress indicators share the host palette and control styling.

Floating menus paint their rounded surface and shadow in a single transparent window. The legacy JUCE rectangular shadow windows are suppressed for these surfaces, and menu items stay inside the shadow gutter. The tracked `patches/juce-popup-surface.patch` reserves 12 pixels of menu padding in the pinned JUCE version; the Windows build applies and verifies the patch automatically.

## Implementation guidance

Use the system UI font, approximately 13-14 pt body text and 22-24 pt page headings. Use an 8-point spacing scale, 32-36 px standard controls, generous effect rows, 10-12 px grouping radii, and subtle one-pixel dividers. Keep selection, keyboard focus, active processing, bypass, and device errors visually distinct. Provide keyboard operation, accessible names for icon buttons and sufficient contrast; information must not depend on color alone.

The chain and library scroll independently. The Windows window supports resizing down to 1000 by 660 content pixels; device settings scroll when needed. Long plugin names truncate within their rows. Missing or unsupported effects are marked Unavailable, and the existing warning explains why loading failed. Plugin editor contents remain controlled by the plugin itself. A collapsible library is a possible future refinement for smaller displays.

## Verification

The Release Windows build and the audio regression suite pass. New library checks cover same-name plugin identities after filtering, case-insensitive manufacturer search, duplicate adds, stale results after library removal, and selection clearing. The Windows UI was inspected at default and minimum sizes using real installed effects with audio devices disabled; adding effects, bypass controls, and opening a native plugin editor were checked.

The follow-up fixes add regression coverage for bypass indication when an effect is unavailable and for old and current imported settings surviving restart without a second path relocation. The final executable was also checked with all six recovered effects and the saved ASIO configuration at 48 kHz and 160 samples; bypass was toggled and restored, compact settings were inspected, and the verification instance exited cleanly.

Multiple-instance checks cover legacy migration, fresh defaults for new copies, separate parameter snapshots, bypass isolation, processor preservation, reordering, removal, restart, and version 3 backup restore. A separate Windows verification profile loaded two actual LoudMax instances with different parameter values and retained both identities and distinct saved snapshots across restart.

The integrated library was checked at default and minimum Windows sizes. Library options stays in the panel, and Scan opens the format selector and folder picker directly; cancellation closes the scan dialogs without opening a library window. Regression checks cover searchable metadata and removing a filtered library entry while keeping both active instances.

Shared-control styling was checked on Windows with an eight-instance LoudMax chain, matching chain/library scrollbars, library and plugin action menus, the checked audio-device menu, and rename/removal dialogs. Canceling those dialogs preserved all eight instances. The Release build and audio/backup regression suite pass.

Popup regression checks verify transparent corners and soft shadow pixels, hover paint staying inside the surface, one native window per menu, content padding, and native-window cleanup after dismissal. A separate Windows app instance confirmed that opening Library options adds exactly one native window and that both library and plugin menus dismiss cleanly.

## Generation brief

Two high-fidelity native desktop UI mockups were generated using the built-in imagegen tool. The first prompt specified Light Host's ordered stereo effect chain, bypass and rename/remove actions, adjacent plugin library, and restrained Apple-inspired typography and surfaces. The second used the first image as its style reference and specified matching audio device settings, app preferences, backup and restore, and saved-state deletion. Both prompts excluded transport, recording, mixer controls, charts, accounts, and unrelated features.

## Dark appearance

[Signal chain](dark-signal-chain-windows.png) · [Settings](dark-settings-windows.png) · [Control states](dark-controls-windows.png)

Dark mode is the default. Settings > App > Dark mode changes the shared palette immediately and saves the preference beside the executable. The chain, library, search, settings, About view, dialogs, popups, scrollbars, and host controls refresh together without recreating the audio graph or plugin instances. Supported Windows versions also use dark native title bars. Plugin editor contents remain controlled by each plugin.

The dark palette follows the supplied iMessage reference: a near-black canvas, neutral gray surfaces, white text, quiet separators, and a clear blue accent. Green, amber, and red retain their status meanings. Regression checks cover repeated dark/light changes, updating an existing library, retaining its search, and text contrast in both appearances.

Dark mode uses a near-black canvas (`#161616`) with matching plugin cards, input/output endpoints, and settings panels outlined by subtle borders (`#3C3C3C`). Buttons retain their gray fill (`#363636`), and active controls and focus outlines use a blue accent (`#0A84FF`). Bypassed power buttons share the plus buttons' gray fill, border, hover tint, and white symbol brightness. Clickable host controls and actionable menu rows use a hand cursor; text fields and resizing retain their usual cursors. Disabled controls and empty list areas retain the arrow cursor.

The Release build and regression suite pass. An isolated Windows profile verified both appearances at default and minimum sizes, preference persistence across restart, and a chain with two real LoudMax instances. The preview used no audio device.
