# wine-uwp

Run unpacked **UWP (Microsoft Store) apps and games** on Linux with a patched Wine 11.22 (development release).

UWP apps don't create a normal Win32 window. They ask Windows for a `CoreApplication`, get a `CoreWindow`, and read their identity, storage, licence and sign-in from WinRT classes that Wine 11.22 mostly lacks. This project adds the missing pieces, without touching the app's files:

- a `CoreApplication`/`CoreWindow` host (window with the app's own icon, fullscreen, lifecycle, keyboard, and mouse input including pointer events, raw mouse motion for mouse look and a hideable cursor)
- package identity read from the app's `AppxManifest.xml`
- `ApplicationData` (local files and settings), `GameSaveProvider` (Xbox connected storage kept on disk), `DisplayRequest`
- an offline Xbox sign-in for your Linux user, and an offline Store licence for the app itself (`StoreContext` and the older `CurrentApp`); Xbox Live web requests fail cleanly as "no network", and a local Xbox Live device address lets games create their sessions
- `MessageDialog` (shown as a small window of its own) and other everyday WinRT classes games ask for
- a partial `Windows.UI.Xaml`, so simple XAML apps start and draw; see [XAML apps](#xaml-apps)
- a DXVK `dxgi.dll` with `CreateSwapChainForCoreWindow`, so D3D12 apps can draw into the CoreWindow

**Tested apps:**

| | App | Status |
|---|---|---|
| ✅ | Halo Wars 2 (`Microsoft.HoganThreshold`, D3D12) | Playable offline: videos, menus, skirmish, native-resolution fullscreen, mouse and keyboard, settings and local saves. See [the example](#example-halo-wars-2). |
| ✅ | Gears of War 4 (`Microsoft.SpartaUWP`, Unreal Engine 4, D3D12) | Campaign playable offline in fullscreen: menus, videos, mouse look and aiming, keyboard; the built-in benchmark runs. No multiplayer or co-op. See [the example](#example-gears-of-war-4). |
| ✅ | Crackdown 3 (`Microsoft.48248D2C77101`, Unreal Engine 4) | Campaign playable offline: menus with the mouse, keyboard movement, mouse look; saves kept locally. Holding the mouse button doesn't skip the intro video; let it play. Its online services fail as offline. |
| ✅ | Microsoft Minesweeper (`Microsoft.MicrosoftMinesweeper`, Unity, XAML, .NET Native) | Playable offline: menus and Classic games with mouse clicks, flags and local saves. Needs `mrt100.dll` (see [framework packages](#set-up-an-app)). After the tutorial, each start waits about 30 seconds on "Downloading Game Data" while its Xbox cloud saves fail as offline. No ads, achievements or cloud saves. |
| ✅ | Microsoft Sudoku (`Microsoft.MicrosoftSudoku`, Unity 6, XAML, .NET Native) | Playable offline: the tutorial and puzzles with the mouse. Its Xbox cloud sync and ads fail as offline. |
| ✅ | Minecraft for Windows 1.21.114 and 1.19.83 (`Microsoft.MinecraftUWP`, D3D11) | Starts and plays offline: menus, worlds. Xbox sign-in and online features (Servers tab, Realms) fail as offline. LAN games work between computers (tested with two Linux PCs; both need this build or later, and a firewall must let UDP 19132, 19133 and 7551 through). Players are named after the Linux user (see [its `apps/` file](apps/Microsoft.MinecraftUWP.conf)). |
| ✅ | Microsoft Mahjong (`Microsoft.MicrosoftMahjong`, XAML, .NET Native) | Playable offline: answer "OK" and "Play Offline" at the download questions, then menus and games with the mouse and keyboard. Its Xbox sign-in, downloads and ads fail as offline. |
| ✅ | Microsoft Ultimate Word Games / Wordament (`Microsoft.Studios.Wordament`, Unity, XAML, .NET Native) | Playable offline. Its [`apps/` file](apps/Microsoft.Studios.Wordament.conf) reports no internet connection, so it doesn't keep asking about Xbox cloud saves. |
| ❌ | Microsoft Solitaire Collection (`Microsoft.MicrosoftSolitaireCollection`, XAML, WinUI 2.8) | Not working: a blank window. Its interface is all XAML with WinUI 2 controls, which the partial XAML doesn't support yet (like Calculator). |
| ❌ | Forza Motorsport 7 (`Microsoft.ApolloBaseGame`, D3D12) | Not working: it starts and its hardware warning is answered, but it isn't playable yet. |
| ❌ | Windows Calculator (`Microsoft.WindowsCalculator`, XAML, WinUI 2.8) | Not working yet: it now gets through start-up and into its main page, then stops because WinUI 2 controls (NavigationView and others) can't yet derive from the XAML Control and ContentControl classes. Work in progress. |

Other native C++ UWP apps may work; many will need WinRT classes this project doesn't have yet. XAML apps (built with `Windows.UI.Xaml` pages and markup) have partial support: simple ones start and work, real Store apps will often still stop or look wrong; see [XAML apps](#xaml-apps). Reports and patches are welcome.

## Quick start: prebuilt release

The [releases page](https://github.com/geoffwoollams/wine-uwp/releases) has the patched Wine, `dxgi.dll` and the D3D12/WinRT DLLs ready to use. No Docker, no compiling, no GE-Proton.

Requirements: x86-64 Linux with a Vulkan GPU and driver that vkd3d-proton supports (recent Mesa RADV/ANV or NVIDIA proprietary), `git`, `curl`, `python3`, and Wine's usual runtime libraries on the host (Vulkan loader, FreeType, X11/Wayland, PulseAudio/ALSA, GnuTLS). The release is built on Debian trixie, so a 2025 or newer distribution is best.

```bash
git clone https://github.com/geoffwoollams/wine-uwp.git
cd wine-uwp
scripts/fetch-release.sh
```

Then start an app straight from its package folder or `.appx`: the first time, it is set up for you (see [Set up an app](#set-up-an-app) for what that does and what apps need).

```bash
scripts/run-app.sh /path/to/MyGame.appx
```

## Build from source instead

Needs Docker (the build runs in a Debian container, so you don't need Wine's build dependencies), about 10 GB of disk, and **GE-Proton 11-14** (for vkd3d-proton and two WinRT DLLs) installed where Steam or Lutris keeps compatibility tools, for example with ProtonUp-Qt, or anywhere if you set `GE_PROTON=/path/to/GE-Proton11-14`.

```bash
git clone https://github.com/geoffwoollams/wine-uwp.git
cd wine-uwp
scripts/build-dxvk.sh      # a few minutes
scripts/build-wine.sh      # 20 to 60 minutes depending on your CPU
```

Both put their output in `build/`, where `fetch-release.sh` would also unpack. Maintainers can then publish a release with [`scripts/package-release.sh`](#publishing-a-release).

## Set up an app

You need the app as an **unpacked package folder** (the folder with `AppxManifest.xml`, `AppxBlockMap.xml` and the executable, for example copied from `C:\Program Files\WindowsApps\<package>`) or as a **package file**: an `.appx`/`.msix`, or an `.appxbundle`/`.msixbundle`, from which the x64 app package is taken. A package file is unpacked once into `~/.local/share/wine-uwp/packages/<package name>/` and set up from there; giving it a newer version of the same app replaces that folder. Encrypted Store downloads (`.eappx`, `.emsix`) can't be used. This project does not provide apps and does not bypass licensing: the app is treated as licensed because Wine has no Store client to ask, so you need to own it.

If you installed a game with **Lutris**, its game folder works too. Lutris puts the files in its own Wine prefix, for example `~/Games/<game>/drive_c/Games/<game name>/`. Pass the folder that holds `AppxManifest.xml` to `setup-prefix.sh`. Starting the game from Lutris still won't work, because Lutris runs it with a Wine that has no UWP support. Start it with `run-app.sh` or the launcher `setup-prefix.sh` offers to add. The game's files are only linked, never changed, so the Lutris install is unaffected.

Apps also need the **framework packages** their `AppxManifest.xml` lists as dependencies: the VC++ runtime (`Microsoft.VCLibs.140.00`) for C++/CX apps, and for some apps .NET Native (`Microsoft.NET.Native.Framework.2.2` and `Microsoft.NET.Native.Runtime.2.2`) or WinUI 2 (`Microsoft.UI.Xaml.2.8`). They are Microsoft's, so releases don't include them.
- **Downloaded automatically:** `setup-prefix.sh` downloads the ones an app needs from Microsoft's own public copies, once, and refuses any file whose SHA-256 doesn't match the known one. The VC++ runtimes (`Microsoft.VCLibs.140.00` 14.0.33519.0 and `.UWPDesktop` 14.0.33728.0) come from the dependency bundle of Microsoft's winget release on GitHub; .NET Native 2.2 (Framework 2.2.29512.0, Runtime 2.2.28604.0) and WinUI 2.8 (8.2501.31001.0) from Microsoft's NuGet packages on nuget.org. Only the `.appx` is read out of each archive (a few MB in all). The list is `FRAMEWORK_DOWNLOADS` in `scripts/uwptool.py`; `--no-download` turns this off.
- **Or from Windows:** a Windows 10/11 install has them in `C:\Program Files\WindowsApps` (folders such as `Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe`). Copy the x64 ones you need into one folder and pass it with `--frameworks`; `.appx`/`.msix` files work too. Use this for a framework not in the list, or an app that needs a newer version. Many games carry their VC++ package in a `redist/` folder (Halo Wars 2, Gears of War 4, Forza Motorsport 7), and those are imported automatically.
- **.NET Native apps** also need `mrt100.dll` from `C:\Windows\System32` of a Windows install: put it in the `--frameworks` folder too (or pass the file itself). It has no public download.
- **Where they go:** one copy of each, in `~/.local/share/wine-uwp/frameworks`. Each app gets the newest stored version of every framework it depends on, linked into its folder; nothing is copied per app. Setup names any framework an app needs that isn't there yet.

```bash
scripts/setup-prefix.sh "/path/to/package-folder"
scripts/setup-prefix.sh /path/to/MyApp.appx       # or a package file
# importing framework packages first (once):
scripts/setup-prefix.sh "/path/to/package-folder" --frameworks /path/to/folder-of-framework-packages
```

This creates the shared Wine prefix on first use, and links the package into it as `C:\UWP\<package name>`. The package folder is never modified. Run it again for each further app; they share the prefix. To give an app a prefix of its own instead, for example when it needs registry settings the others shouldn't see, add `--prefix /path/to/prefix`. That folder is remembered, so `run-app.sh`, launchers and later setups use it too. It can also be an existing prefix, such as one Lutris made, but wine-uwp's Wine upgrades it on first use and setup replaces its `dxgi`, D3D12 and WinRT DLLs, so the tool that made it may not run it properly afterwards. Add `--verify` to first check every file against the hashes in `AppxBlockMap.xml`, which catches a damaged or incomplete copy (a few minutes for a big app).

At the end it asks whether to **add a launcher** for the app: a `.desktop` file in your applications menu (`~/.local/share/applications/wine-uwp-<package name>.desktop`) and a copy on your desktop, with the app's own icon taken from its package. Pass `--shortcut` to add it without asking, or `--no-shortcut` to skip the question, for example in scripts. To remove a launcher, delete those two files.

Play, either from the launcher or from a terminal:

```bash
scripts/run-app.sh                       # pick from a numbered list (or start the only app set up)
scripts/run-app.sh Microsoft.HoganThreshold
scripts/run-app.sh /path/to/package-folder   # or an .appx: set up the first time, then started
```

Given a package folder or package file, `run-app.sh` runs `setup-prefix.sh` on it first unless the app is already set up from that folder (a package file is unpacked into `~/.local/share/wine-uwp/packages/` once), so one command takes an app from download to running. Without a package name and with several apps set up, `run-app.sh` lists them by name and package, one per line, and asks for a number.

**Controllers:** games see a controller when it looks like an Xbox (XInput) gamepad. Wine's SDL backend presents every controller SDL recognises that way, including PlayStation and Switch pads, because `setup-prefix.sh` turns off Wine's raw-HID handling for Sony (`054C`) and Nintendo (`057E`) devices in the prefix. To get a raw device back, set `Hidraw` to 1 under `HKLM\System\CurrentControlSet\Services\WineBus\Devices\<vendor id>`. Steam's controller handling also works, since it presents an Xbox pad. Controllers SDL doesn't recognise as gamepads reach games only as raw controllers, which most games ignore; an `SDL_GAMECONTROLLERCONFIG` mapping for the device fixes that.

The game window shows the app's icon in the taskbar, in Alt-Tab and in its title bar, and the desktop groups it under the launcher's icon. This works under X11 and under Wayland through XWayland, which is how Wine runs on a Wayland desktop by default. With Wine's own Wayland driver it needs a compositor that supports the `xdg-toplevel-icon` protocol (KDE Plasma 6.1 or later, for example). Elsewhere the launcher's icon still shows.

Everything lives under `~/.local/share/wine-uwp/` (the prefix, logs and launcher icons) and the repo's `build/` folder, plus any launchers you added. Delete those to uninstall.

## Upgrading

To move to a newer release, run:

```bash
git pull
scripts/fetch-release.sh
```

After unpacking the new build, `fetch-release.sh` sets up every app you have set up again (in the shared prefix or its own), with `setup-prefix.sh --update-all`. That copies in the new `dxgi.dll` and D3D12 DLLs, registers the WinRT classes the new build adds, and relinks each package from the folder it was set up from. It doesn't ask the launcher question; your saves, settings and launchers are kept. If you build from source instead, run it yourself after `build-wine.sh`:

```bash
scripts/setup-prefix.sh --update-all
```

An app whose package folder has moved or is gone is skipped with a warning. Set it up again with `scripts/setup-prefix.sh /new/path/to/it`.

## XAML apps

Wine has no `Windows.UI.Xaml`, the UI framework most non-game UWP apps are built on. This project adds a **partial** implementation, in stages; what works so far is listed below.

**Milestone 1 (done): code-only XAML apps.** `Application.Start`, an app's own `App` class deriving from `Application` (with `OnLaunched` called on launch), `Application.Current`, `RequestedTheme` (light or dark background), `Window.Current` (`Content`, `Activate`, `Bounds`, `Dispatcher`, `Close`), and a `TextBlock` (`Text`, `FontSize`, `FontWeight`, `FontStyle`, `Foreground`, `TextWrapping`, `TextAlignment`, `Padding`) with `Margin`, `Width`/`Height` and alignment, drawn in the CoreWindow. Brushes are `SolidColorBrush` only. Rendering is GDI, so text is anti-aliased but there are no transforms or opacity yet.

**Milestone 2 (done): layout and buttons.** Measure/arrange layout with `StackPanel`, `Grid` (Auto, pixel and star rows and columns, spans), `Border`, and `Button` with the Windows 10 look, hover and pressed states, and `Click` from the mouse. Elements can be subclassed by apps (aggregation), and `Panel.Children` and Grid row/column definitions are real collections.

**Milestone 3 (done): compiled markup.** `Application.LoadComponent` loads the app's `.xbf` files (compiled XAML, version 2), so an app laid out like Visual Studio's Blank App starts. The markup can use the controls above, `Page`, `UserControl` and `Frame` (with `Navigate` creating the app's pages through its `IXamlMetadataProvider`), `x:Name` and `Click` handlers, `Application.Resources` and `{StaticResource}`, and common `{ThemeResource}` brushes. Microsoft's own compiler-made test files load.

**Milestone 4, part 1 (done): styles.** Explicit and implicit `Style`s with `BasedOn` (local values still win), theme dictionaries for `{ThemeResource}`, merged dictionaries and `ResourceDictionary Source="ms-appx:///Styles.xaml"`.

**Milestone 4, part 2 (done): control templates.** `ControlTemplate` (directly or from a style), `TemplateBinding`, `ContentPresenter`, and `VisualStateManager` states with setters and storyboards (which jump to their final values), so a templated Button changes look on hover and press.

**Milestone 4, part 3 (done): toggle controls.** `ToggleButton`, `CheckBox` (including three-state) and `RadioButton` (groups by parent or `GroupName`), with `Checked`/`Unchecked`/`Indeterminate` events and templates whose check states follow WinUI's naming. `TemplateBinding` now follows later changes on the control. Built-in looks use the WinUI resource keys an app overrides, such as `ButtonBackgroundPointerOver` or `CheckBoxCheckBackgroundFillChecked`.

**Milestone 4, part 4 (done): XAML text and default styles.** `XamlReader.Load` reads XAML text. A `generic.xaml` built into the DLL supplies Windows 10's theme brushes, the standard text styles (`TitleTextBlockStyle` and the rest) and default styles. Button and ToggleButton get real default templates, so apps that override their resource keys or restyle them see the right result. Translucent theme brushes are blended.

**Milestone 4, part 5 (done): shapes, corners and themes.** `Rectangle`, `Ellipse` and `Path`, `CornerRadius`, and default CheckBox and RadioButton templates built from shapes. Theme changes while the app runs take effect: `RequestedTheme` on an element (the usual in-app dark mode switch) and the system's light or dark setting, when the app hasn't fixed its theme. `LoadComponent` reads loose `.xaml` files when there is no compiled `.xbf`.

**Milestone 4, part 6 (done): data and motion.** `{Binding}` (DataContext, ElementName, TwoWay, converters, `INotifyPropertyChanged`), `DataTemplate` (ContentTemplate, `ItemsControl` with `ItemsSource` and `ItemTemplate`), `ToggleSwitch`, `Slider` and `ProgressBar`, storyboards that run over time (visual states, and `Storyboard.Begin` from code), `Opacity`, and Path curves and geometries.

**Direct2D (done):** frames are drawn with Direct2D and DirectWrite (anti-aliased shapes and text), with `RenderTransform`, `Clip` and `LinearGradientBrush`; GDI remains the fallback.

**Milestone 5, part 1 (done): scrolling and the keyboard.** `ScrollViewer` with the mouse wheel, keys, Windows 10 scroll bars, `ChangeView` and `ViewChanged`. Keyboard focus with `Focus`, `IsTabStop`, `TabIndex`, a focus visual and Tab / Shift+Tab, routed `KeyDown`/`KeyUp`/`GotFocus`/`LostFocus`, Space and Enter on buttons, keys on sliders and toggle switches, and `FocusManager`.

**Milestone 5, part 2 (done): text entry.** `TextBox` and `PasswordBox` with typing, selection, the caret, the clipboard, multi-line text with `AcceptsReturn`, `MaxLength`, `IsReadOnly`, `Header`, `PlaceholderText`, `TextChanged` and `PasswordChanged`. No IME or undo yet.

**Milestone 5, part 3 (done): pictures.** `Image` with `Stretch`, `BitmapImage` from `ms-appx:` files (PNG, JPEG, BMP, GIF and the other formats WIC reads) with `DecodePixelWidth`/`Height`, `ImageOpened`/`ImageFailed`, and `ImageBrush` backgrounds and fills.

**Milestone 5, part 4 (done): lists.** `ListView` and `GridView` with item containers, scrolling, `SelectionMode` (single, multiple, extended with Ctrl and Shift), `SelectedIndex`/`SelectedItem`/`SelectedItems`, `SelectionChanged`, `ItemClick`, arrow-key navigation, and the `ItemsStackPanel` and `ItemsWrapGrid` panels. No virtualization, grouping or drag and drop yet.

**Milestone 5, part 5a (done): dialogs.** `ContentDialog` with `ShowAsync`, title, content, up to three buttons, `DefaultButton`, Enter and Escape, `Closing`/`Closed` with `Cancel`, over a modal layer.

**Milestone 5, part 5b (done): command bars.** `CommandBar` with primary commands, a "..." overflow menu of secondary commands and light dismiss, `AppBarButton`, `AppBarToggleButton`, `AppBarSeparator`, `SymbolIcon` and `FontIcon` (Segoe MDL2 Assets glyphs, or Unicode stand-ins without that font), and `Page.TopAppBar`/`BottomAppBar`. That completes milestone 5.

**Not yet:** flyouts, menus and the rest of milestone 6, `x:Bind` beyond what its generated code calls, localized `x:Uid` strings and navigation history. Real Store apps will still often stop or look wrong where they need these.

To try it, build the test clients and run them in a throwaway prefix (each opens a window for a few seconds and checks what was drawn):

```bash
tests/xaml/build.sh
```

```bash
tests/xaml/run.sh
```

## Options

| Variable | Effect |
|---|---|
| `WINE_CORE_WINDOWED=1` | Run in a normal 1280x720 window instead of borderless fullscreen |
| `WINE_CORE_SCALE=1.5` | Report a display scale (like Windows' 150% setting); leave unset for 100% |
| `WINE_APP_STORE_ID=...` | The Store ID reported for the app (it is not in the package manifest); apps that care need it set in [`apps/<package name>.conf`](apps/README.md) |
| `WINE_MESSAGEDIALOG_AUTO=Continue` | Answer an app's pop-up dialogs without showing them: the first button whose label starts with this text (or a button number). Set for Gears of War 4 and Forza Motorsport 7 in their `apps/` files, to get past their hardware warnings; `WINE_MESSAGEDIALOG_AUTO=` (empty) shows them again |
| `WINE_UWP_OFFLINE=1` | Report no internet connection to the app (`NetworkInformation`), as Windows does offline. Set for apps that otherwise keep retrying their online services; Xbox Live itself always looks unreachable to apps |
| `WINE_UWP_GAMERTAG=name` | The player name apps see for the offline Xbox user, and Minecraft's LAN name (default: your Linux user name). The user ID always differs between computers, so two people on the same LAN don't clash. |
| `WINE_REPORT_HOSTNAMES=1` | Report the PC's real IP addresses to the app (off by default) |
| `WINEDEBUG=-all` | Turn off logging (default logs errors, fixmes and the WinRT channel) |
| `UWP_PREFIX`, `UWP_DATA_DIR`, `UWP_LOGS`, `UWP_BUILD_DIR` | Move the shared prefix, data, logs or build folders |
| `UWP_RELEASE_REPO` | GitHub `owner/repo` that `fetch-release.sh` downloads from (default: this project) |
| `GE_PROTON` | Path to GE-Proton 11-14 when building from source, if it isn't found automatically |

## Saves and settings

Local files and settings are written where Windows would put them, inside the prefix: `drive_c/users/<you>/AppData/Local/Packages/<package family name>/` (`LocalState` for the app's files, `Settings` for its settings store). Xbox connected storage is kept there too, under `LocalState/XboxGameSaves/<service config id>/<container>/<blob>`, because there is no Xbox Live to sync it to. They persist between runs; back that folder up if you care about it.

## Limitations

- **Offline only.** You are signed in as your Linux user name; there is no Xbox Live, multiplayer or LAN.
- **No add-ons.** Wine has no Store client, so only the app's own licence is reported; add-ons and DLC that require a licence are unavailable.
- **Xbox cloud saves stay on this PC.** Nothing is synchronised with Xbox Live, so saves from Windows or Xbox do not appear, and the reverse.
- **The screen can still blank.** `DisplayRequest` asks Wine to keep the display on, but Wine 11.22 does not pass that on to the desktop, so a screensaver or display power-off may still happen.
- **Fullscreen or windowed is chosen at launch.** The window is created borderless at your screen's resolution, or as a 1280x720 window with a title bar with `WINE_CORE_WINDOWED=1` or when the app last asked to launch windowed (its `PreferredLaunchWindowingMode`, which casual games such as Minesweeper set; it is kept per app in `HKCU\Software\Wine\WindowsRuntime\Packages\<package name>`). Its style is not changed while you play, because resizing a mapped window under its Vulkan swapchain can leave it without mouse input under XWayland. An app's own windowed-mode option may lose the mouse the same way; set `WINE_CORE_WINDOWED=1` instead.
- **Resource strings.** `ms-resource:` names in the manifest are not resolved (no `resources.pri` support), so an app with such a display name gets its package name as the window title.
- **Only D3D12 apps are covered** by the graphics setup; D3D11 or other renderers on a CoreWindow are untested.

## Example: Halo Wars 2

Package `Microsoft.HoganThreshold` 1.11.2931.2, x64, DirectX 12, a native C++/CX game. The game files are used exactly as shipped; `setup-prefix.sh --verify` checks all of them against `AppxBlockMap.xml` and refuses to continue if any differ.

```bash
scripts/setup-prefix.sh "/path/to/Halo Wars 2"
scripts/run-app.sh Microsoft.HoganThreshold
```

Things specific to this game:

- Its Store ID is set in [`apps/Microsoft.HoganThreshold.conf`](apps/Microsoft.HoganThreshold.conf).
- In-game, keep **Resolution Scale at 100%**. Higher values render above your screen resolution and scale down, which costs a lot of performance.
- The game renders at 1280x720 for the first moments of startup, stretched to the screen, until it switches to your full resolution. A few seconds of black screen while it starts and compiles shaders is normal; the first launch takes longest.
- **Crash on quit.** The game's network code faults during shutdown because its network subsystem never came up. It happens after you choose to quit, so nothing is lost; the backtrace lands in the log folder.
- It keeps a "Progression" container in Xbox connected storage (blobs such as `data`, `LeaderboardEntries` and `keyremaps`); see [Saves and settings](#saves-and-settings).

## Example: Gears of War 4

Package `Microsoft.SpartaUWP`, x64, DirectX 12, an Unreal Engine 4 game of over 60 GB. Its executable is in a subfolder (`GearGame\Binaries\UWP64\GearGame.exe`), which `setup-prefix.sh` handles; the game finds its content from the package root.

```bash
scripts/setup-prefix.sh "/path/to/Gears of War 4"
scripts/run-app.sh Microsoft.SpartaUWP
```

Things specific to this game:

- **Run it fullscreen** (the default, without `WINE_CORE_WINDOWED=1`): in a window the mouse may not reach the game; see [Limitations](#limitations).
- At start it warns, in a **"Thank you for installing Gears of War 4"** dialog, that the graphics driver has known compatibility problems. It reads the driver version the way Windows reports it, which Wine doesn't match. [`apps/Microsoft.SpartaUWP.conf`](apps/Microsoft.SpartaUWP.conf) answers it with Continue, so you won't see it.
- It then says the **Xbox user and the Store user don't match**, because both are offline stand-ins here. It carries on.
- The campaign and the benchmark work. Everything online (multiplayer, Horde, co-op, cards, profile stats) fails as offline. The game keeps retrying Xbox Live in the background, which shows in the log but does no harm.
- The first launch compiles a lot of shaders, so expect long loads and some stutter the first time through a level.

## Troubleshooting

Each run writes `~/.local/share/wine-uwp/logs/run-<package>-<time>.log` (the last 8 are kept; `latest.log` points at the newest). The last line says how the app exited. If it crashed, a backtrace is saved next to it as `run-<package>-<time>.crash.txt`. Include both when reporting a problem.

## Publishing a release

For maintainers. After building (see above) and committing, with GE-Proton 11-14 installed:

```bash
scripts/build-wine.sh
scripts/package-release.sh
gh release create 11.22-uwp4 --title wine-11.22-uwp4 dist/wine-uwp-x86_64.tar.xz dist/wine-uwp-x86_64.tar.xz.sha256 --notes-file dist/NOTES.md
```

**Version numbers** look like `11.22-uwp4`: the Wine release the patches are based on, then wine-uwp's own version, a whole number. Raise `UWP_VERSION` in `scripts/common.sh` by one for each GitHub release (`4`, `5`, ...; releases before 4 were `3.0`, `v2.1` and so on) and change `WINE_TAG` when moving to a newer Wine; `RELEASE_VERSION` is made from both. `build-wine.sh` tags its source checkout so that `wine --version` reports `wine-11.22-uwp4`, and `package-release.sh` refuses a build whose version doesn't match. The download keeps the same file name, `wine-uwp-x86_64.tar.xz`, in every release, which is what lets `fetch-release.sh` find the latest one.

The archive contains the Wine install tree, DXVK's `dxgi.dll`, `d3d11.dll` and `d3d10core.dll` (D3D11 games and XAML's Direct2D drawing need them with that dxgi), the four component DLLs (`d3d12.dll`, `d3d12core.dll`, `wintypes.dll`, `windows.storage.dll`), the licence texts, `SOURCE.txt` (where to get every part's source) and a copy of `patches/`, `docker/` and `scripts/` (the recipe that built it). The script refuses to run with uncommitted changes in those folders, and refuses a Wine build containing add-on unlocking code.

## Licences

Everything in a release is free software, and a release contains no Microsoft software and no app files. Framework packages (the VC++ runtime, .NET Native, WinUI) come from your own copies at setup time and are never redistributed here.

This repository is licensed under the **GNU LGPL 2.1 or later**, the same as Wine ([LICENCE](LICENCE)), except the DXVK patch, which is zlib like DXVK. You can use, change and share the patches, scripts and tests under those terms.

| Part | Licence |
|---|---|
| Wine and the patches in `patches/wine/` | LGPL 2.1 or later |
| The scripts, tests, docker files and app settings in this repository | LGPL 2.1 or later |
| DXVK `dxgi.dll`, `d3d11.dll`, `d3d10core.dll` and the patches in `patches/dxvk/` | zlib |
| vkd3d-proton `d3d12.dll`, `d3d12core.dll` and Wine's `wintypes.dll`, `windows.storage.dll` (from GE-Proton) | LGPL 2.1 or later |

This is not legal advice. The LGPL requires that anyone who gets the binaries can get the matching source; the release's `SOURCE.txt` and bundled `source/` folder do that.

## Repository layout

```
patches/wine/   patches against the wine-11.22 tag (apply in order with git apply): 0001-0007 the UWP host and
                services (0004 replaces Wine's stub CoreApplication factory; 0007's JsonObject works with Wine's
                JsonValue and JsonArray), 0008 CurrentApp, 0009 DisplayRequest, 0010 GameSaveProvider,
                0011 class registration (CoreWindow and DisplayInformation come from twinapi.appcore, not Wine's
                stubs), 0012 no hard-coded Store ID, 0013-0016 Windows.UI.Xaml milestone 1 (widl fix,
                generated IDL, ProcessUntilQuit, the XAML code), 0017-0018 milestone 2 (IDL, layout, panels,
                Button), 0019-0022 milestone 3 (widl registration fix, IDL, launch args, markup loading),
                0023 milestone 4 part 1 (styles, theme and merged dictionaries), 0024-0025 part 2 (control
                templates, visual states), 0026-0027 part 3 (ToggleButton, CheckBox, RadioButton, live
                TemplateBinding), 0028-0029 part 4 (XAML text, XamlReader, generic.xaml),
                0030-0031 part 5 (shapes, corners, theme changes, loose XAML), 0032-0033 part 6
                (ToggleSwitch, Slider, ProgressBar, ItemsControl, DataTemplate, {Binding}, animation timing),
                0034-0035 Direct2D rendering (transforms, clips, gradients), 0036-0039 Gears of War 4
                (CoreApplication error and view interfaces, package root lookup, LoadPackagedLibrary,
                MouseDevice, MessageDialog, Xbox Live device address, msxml6 HTTP class registration,
                pointer devices and cursor; COM ContextCallback is Wine's own since 11.x), 0040 the app's icon
                on its window, 0041-0042 Forza Motorsport 7 and automatic dialog answers, 0043-0044 XAML
                milestone 5 part 1 (ScrollViewer, keyboard focus, key events), 0045-0046 part 2 (TextBox,
                PasswordBox), 0047-0048 part 3 (Image, BitmapImage, ImageBrush), 0049-0050 part 4 (ListView,
                GridView, their items and panels), 0051-0052 part 5a (ContentDialog; 0051 has all of part 5's IDL),
                0053 part 5b (CommandBar, app bar buttons, icons), 0054 compiled XAML read from resources.pri
                and app pages from C++/CX TypeNames, 0055-0060 for Microsoft Minesweeper (a Unity XAML game):
                PointerVisualizationSettings, PnpObject, SystemNavigationManager, input capabilities,
                ICoreApplication2, MediaDevice selectors, the thread pool's IAsyncAction, SwapChainPanel,
                CompositionTarget, Loaded/SizeChanged, 0061 delay-load failure handling and
                SetProcessValidCallTargets (.NET Native's runtime), 0062 ExtendedExecution's
                async operation interfaces, 0063 an offline WinRT HTTP filter (requests fail as
                "name not resolved"), 0064 store objects' interfaces, 0065 StorageFolder
                CreateFolderAsync/GetFolderAsync, 0066 PlatformDiagnosticsAndUsageDataSettings, 0067
                twinapi shims answering only their own interfaces and CoreDispatcher.RunIdleAsync, 0068
                ApiInformation answers and HighContrastChanged, 0069 StorageFile and file streams,
                0070 Clipboard text, 0071 GetProxyConfigurationAsync, 0072 ApiInformation from
                twinapi.appcore (GE-Proton's wintypes fails its checks) and signed Xbox Live tokens, 0073-0078
                for Crackdown 3: CPU set queries, DisplayInformation 3 and 4, the add-on licence map,
                GameChatOverlay, CryptographicBuffer, gaming privilege checks, IMediaPlayer3 and
                MediaPlaybackSession, 0079 AcceleratorKeyActivated (Unreal Engine's game keys) and CoreWindow key
                state from any thread, 0080 the *FromApp file functions, 0081-0083 for Microsoft Sudoku
                (Unity 6): a package's own WinRT classes first (Unity games no longer load each other's),
                DispatcherQueue.GetForCurrentThread, ResourceContext, 0084 FileIO, 0085 hash algorithms, 0086
                classes from a package's namespace-named DLLs, 0087 ThreadPoolTimer lifetime (a game's main loop
                hung on a timer that never fired), 0088-0093 for Minecraft: ResolveDelayLoadsFromDll,
                DeviceInformation.CreateFromIdAsync, InputPane events, ICoreDispatcherWithTaskPriority,
                CoreTextEditContext, LocalCacheFolder and publisher cache folders, 0094-0095 for Minecraft
                1.19: PnpObject.CreateFromIdAsync, HolographicApplicationPreview, 0096-0098 for Microsoft
                Mahjong (not working yet): XAML SizeHelper, PointHelper, Canvas and more FrameworkElement members,
                SpatialAudio classes, device information, CoreTextEditContext2, 0099 game controllers: a
                controller's User and its events (Minecraft stopped when a pad was connected), 0100 ResourceManager over
                the package's resources.pri (.NET Native's resx strings; Mahjong stopped after "Play Offline"), 0101
                relative paths in StorageFolder.GetFileAsync and the like, 0102-0105 for Mahjong: AppInfo,
                DateTimeFormatter, KeyRoutedEventArgs.OriginalKey, and a title bar for apps launched windowed, 0106-0108
                for Wordament: Store and user vectors answer only their own interfaces, Xbox Live hosts don't
                resolve in apps (their placeholder token was refused with 401), WINE_UWP_OFFLINE, 0109
                CoreWindow.Activated raised again once the app has subscribed (the first click was ignored), 0110-0113
                towards Calculator: real DependencyProperty/PropertyMetadata/DependencyObject (app-registered
                properties, change callbacks, Grid and Canvas properties), App.xaml loaded for C# apps, PropertyValue
                class names (.NET unboxing), ResourceLoader, NamedPolicy, SplashScreen, LanguageFontGroup
                0114-0115 for Minecraft LAN games: the computer's own name resolves to its network addresses (RakNet
                broadcast only to 127.0.1.255), and the offline Xbox user ID differs between computers, 0116
                ConnectionCost (Minecraft disabled multiplayer while it failed)
patches/dxvk/   patches against DXVK f90f1bf1 (v3.1.1-235): CoreWindow and composition swap chains
docker/         build containers
apps/           optional per-app settings
scripts/        fetch-release.sh, build-wine.sh, build-dxvk.sh, setup-prefix.sh, run-app.sh,
                package-release.sh, uwptool.py, winmd2idl.py (WinRT IDL from Windows metadata),
                xbf.py (dump compiled XAML, or compile test XAML)
tests/xaml/     the XAML test clients (build.sh, run.sh): xamlhello (milestone 1), xamllayout (milestone 2),
                xamlmarkup + markup/ (milestones 3 and 4)
```
