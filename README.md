# wine-uwp

Run **UWP (Microsoft Store) apps and games** on Linux with a patched Wine 11.22 (development release).

UWP apps don't create a normal Win32 window. They ask Windows for a `CoreApplication`, get a `CoreWindow`, and read their identity, storage, licence and sign-in from WinRT classes that Wine 11.22 mostly lacks. This project adds the missing pieces, without touching the app's files:

- a `CoreApplication`/`CoreWindow` host (window with the app's own icon, fullscreen, lifecycle, keyboard, and mouse input including pointer events, raw mouse motion for mouse look and a hideable cursor)
- package identity read from the app's `AppxManifest.xml`
- `ApplicationData` (local files and settings), `GameSaveProvider` (Xbox connected storage kept on disk), `DisplayRequest`
- an offline Xbox sign-in for your Linux user, and an offline Store licence for the app itself (`StoreContext` and the older `CurrentApp`); Xbox Live web requests fail cleanly as "no network", and a local Xbox Live device address lets games create their sessions
- `MessageDialog` (shown as a small window of its own) and other everyday WinRT classes games ask for
- a partial `Windows.UI.Xaml`, so simple XAML apps start and draw; see [XAML apps](#xaml-apps)
- a DXVK `dxgi.dll` with `CreateSwapChainForCoreWindow`, so D3D12 apps (through vkd3d-proton) and D3D11 apps (through DXVK's `d3d11.dll`) can draw into the CoreWindow

**Tested apps:**

| | App | Status |
|---|---|---|
| ✅ | Minecraft for Windows (`Microsoft.MinecraftUWP`, D3D11) | Starts and plays offline: menus, worlds. Xbox sign-in and online features (Servers tab, Realms) fail as offline. LAN games work between computers (tested with two Linux PCs; firewall must let UDP 19132, 19133 and 7551 through). Players are named after the Linux user (see [its `apps/` file](apps/Microsoft.MinecraftUWP.conf)).<br><br>✅ 1.21.114<br>✅ 1.19.83<br>✅ 1.16.221.1<br>❌ 1.0.0.16<br>❓ 0.14.0.1|
| ✅ | Halo Wars 2 (`Microsoft.HoganThreshold`, D3D12) | Playable offline: videos, menus, skirmish, native-resolution fullscreen, mouse and keyboard, settings and local saves. See [the example](#example-halo-wars-2). |
| ✅ | Gears of War 4 (`Microsoft.SpartaUWP`, Unreal Engine 4, D3D12) | Campaign playable offline in fullscreen: menus, videos, mouse look and aiming, keyboard; the built-in benchmark runs. No multiplayer or co-op. See [the example](#example-gears-of-war-4). |
| ✅ | Crackdown 3 (`Microsoft.48248D2C77101`, Unreal Engine 4) | Campaign playable offline: menus with the mouse, keyboard movement, mouse look; saves kept locally. Holding the mouse button doesn't skip the intro video; let it play. Its online services fail as offline. |
| ✅ | Microsoft Minesweeper (`Microsoft.MicrosoftMinesweeper`, Unity, XAML, .NET Native) | Playable offline: menus and Classic games with mouse clicks, flags and local saves. Needs `mrt100.dll` (see [framework packages](#set-up-an-app)). After the tutorial, each start waits about 30 seconds on "Downloading Game Data" while its Xbox cloud saves fail as offline. No ads, achievements or cloud saves. |
| ✅ | Microsoft Sudoku (`Microsoft.MicrosoftSudoku`, Unity 6, XAML, .NET Native) | Playable offline: the tutorial and puzzles with the mouse. Its Xbox cloud sync and ads fail as offline. |
| ✅ | Microsoft Mahjong (`Microsoft.MicrosoftMahjong`, XAML, .NET Native) | Playable offline: answer "OK" and "Play Offline" at the download questions, then menus and games with the mouse and keyboard. Its Xbox sign-in, downloads and ads fail as offline. |
| ✅ | Microsoft Ultimate Word Games / Wordament (`Microsoft.Studios.Wordament`, Unity, XAML, .NET Native) | Playable offline. Its [`apps/` file](apps/Microsoft.Studios.Wordament.conf) reports no internet connection, so it doesn't keep asking about Xbox cloud saves. |
| ✅ | Asphalt 7: Heat (`GAMELOFTSA.Asphalt7HeatByGameloft`, D3D11, XAML `SwapChainBackgroundPanel`) | Playable offline: menus, races and keyboard driving (choose "Keyboard" under controls in its settings: there's no tilt sensor). Xbox Live and its network features fail as offline. Its [`apps/` file](apps/GAMELOFTSA.Asphalt7HeatByGameloft.conf) makes it use the package's own `msvcr110.dll`/`msvcp110.dll`. |
| ❌ | Forza Motorsport 7 (`Microsoft.ApolloBaseGame`, D3D12) | Not working: it starts and its hardware warning is answered, but it isn't playable yet. |
| ❌ | Microsoft Solitaire Collection (`Microsoft.MicrosoftSolitaireCollection`, XAML, WinUI 2.8) | Not working: a blank window. Its interface is all XAML with WinUI 2 controls, which the partial XAML doesn't support yet (like Calculator). |
| ❌ | Windows Calculator (`Microsoft.WindowsCalculator`, XAML, WinUI 2.8) | Not working yet: it now gets through start-up and into its main page, then stops because WinUI 2 controls (NavigationView and others) can't yet derive from the XAML Control and ContentControl classes. Work in progress. |

Other native C++ UWP apps may work; many will need WinRT classes this project doesn't have yet. XAML apps (built with `Windows.UI.Xaml` pages and markup) have partial support: simple ones start and work, real Store apps will often still stop or look wrong; see [XAML apps](#xaml-apps). Reports and patches are welcome.

## Recommended: UWP Launcher

The easiest way to use wine-uwp is **[UWP Launcher](https://github.com/geoffwoollams/wine-uwp-gui)**, its graphical front end. It is one file: download `wine-uwp-gui-x86_64` from its [releases](https://github.com/geoffwoollams/wine-uwp-gui/releases) and run it.

```bash
chmod +x wine-uwp-gui-x86_64 && ./wine-uwp-gui-x86_64
```

It offers to add itself to your applications menu under Games, then downloads wine-uwp's latest release and keeps it up to date (no git needed). From there you install apps from package files, folders or install discs (ISOs), or add the ones already on this PC, and play them from a library with their artwork, play time, logs, per-app options, versions kept side by side, and Steam shortcuts. It uses the same data folder as the scripts below, so you can use both. It needs wine-uwp 11.22-uwp6 or later.

The rest of this README covers the scripts themselves, which UWP Launcher runs for you.

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

You need the app as an **unpacked package folder** (the folder with `AppxManifest.xml`, `AppxBlockMap.xml` and the executable, for example copied from `C:\Program Files\WindowsApps\<package>`) or as a **package file**: an `.appx`/`.msix`, or an `.appxbundle`/`.msixbundle`, from which the x64 app package is taken. A package file is unpacked once into `~/.local/share/wine-uwp/packages/<package name>/` and set up from there; giving it a newer version of the same app replaces that folder. A folder made by unzipping an `.appx` with an ordinary unzip tool works too: such tools keep the package's percent-encoded file names (`edit%400.5x.png` for `edit@0.5x.png`), and setup links those files under their real names, so the app finds them (Minecraft's menu icons were white boxes otherwise). Encrypted Store downloads (`.eappx`, `.emsix`) can't be used. This project does not provide apps and does not bypass licensing: the app is treated as licensed because Wine has no Store client to ask, so you need to own it.

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

`scripts/list-apps.sh` lists the apps set up, with their names, versions and version slots; `--json` prints the details (names, images, folders, versions) for front ends such as [UWP Launcher](https://github.com/geoffwoollams/wine-uwp-gui).

**Several versions of one app.** Only one version of a package name is set up at a time, but versions can be kept side by side, each with its own saves, settings and logs. Set each one up with `--data-slot NAME` (usually its version), and switch between them with `--switch`:

```bash
scripts/setup-prefix.sh ~/Games/Minecraft/1.21.114.1 --data-slot 1.21.114.1
scripts/setup-prefix.sh "Minecraft 1.19.83.1.appx" --data-slot 1.19.83.1
scripts/setup-prefix.sh --switch Microsoft.MinecraftUWP 1.21.114.1
```

The app's saves folder (see [Saves and settings](#saves-and-settings)) becomes a link to `~/.local/share/wine-uwp/versions/<package>/<slot>/data`. The first time, the version set up until then becomes a slot named after its version and keeps the saves made so far. A package file set up with a slot is unpacked into the slot's folder, so versions don't replace each other. Setting an app up again without `--data-slot` keeps the slot it has, so a newer copy of the same version replaces it in place.

**Removing an app:** `scripts/remove-app.sh <package name>` removes what wine-uwp made for it: its folder of links in the prefix, its WinRT class registrations, its launchers, its settings files and the copies unpacked from package files. Its saves are kept unless you add `--data`, and the folder you set it up from (a copy from Windows, a Lutris install) is never touched: the script prints its path. `--slot <slot>` removes one kept version instead. It asks first; `--yes` doesn't.

Everything lives under `~/.local/share/wine-uwp/` (the prefix, logs and launcher icons) and the repo's `build/` folder, plus any launchers you added. Delete those to uninstall everything.

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

Wine has no `Windows.UI.Xaml`, the UI framework most non-game UWP apps are built on. This project adds a **partial** implementation: enough for Visual Studio-style XAML apps, Unity games hosted in XAML, and several Microsoft casual games. Larger Store apps may still stop or look wrong where they need something missing.

**App model.** `Application` (start-up, `OnLaunched`, `OnWindowCreated`, `LeavingBackground`, `Application.Current`, `Resources`, `RequestedTheme`) and `Window` (`Current`, `Content`, `Activate`, `Activated`, `VisibilityChanged`, `Bounds`, `Dispatcher`). Apps' own classes can derive from the framework's (`App`, pages, user controls, controls). `DispatcherTimer`, `CompositionTarget` and `SwapChainPanel` (how Unity draws inside a XAML page).

**Markup and resources.** Compiled XAML (`.xbf`) from the package's files or `resources.pri`, loose `.xaml` files, `XamlReader.Load` and `Application.LoadComponent`. `x:Name`, event handlers, localized `x:Uid` strings, `{StaticResource}` and `{ThemeResource}`, explicit and implicit styles with `BasedOn`, theme and merged dictionaries, and a built-in `generic.xaml` with Windows 10's theme colours, text styles and default control styles. `{Binding}` (DataContext, ElementName, TwoWay, converters, `INotifyPropertyChanged`), `x:Bind` through its generated code, dependency properties and attached properties registered by apps, and app-defined properties reached through the app's XAML metadata.

**Layout.** `StackPanel`, `Grid` (Auto, pixel and star sizes, spans), `Canvas`, `Border`, `Viewbox`, `ScrollViewer` (mouse wheel, keys, scroll bars, `ChangeView`), margins, alignment, and minimum and maximum sizes.

**Controls.** `TextBlock`, `Button`, `ToggleButton`, `CheckBox`, `RadioButton`, `ToggleSwitch`, `Slider`, `ProgressBar`, `TextBox` and `PasswordBox` (selection, clipboard, multi-line), `Image` with `BitmapImage` (formats WIC reads), `ItemsControl`, `ListView` and `GridView` (selection modes, item templates, keyboard navigation), `ContentDialog`, `Flyout`, `CommandBar` with app bar buttons, `SymbolIcon` and `FontIcon`, and `Page`, `UserControl` and `Frame` (navigation with back and forward history).

**Templates and states.** `ControlTemplate`, `TemplateBinding`, `ContentPresenter`, `DataTemplate`, and `VisualStateManager` states for templates and user controls (`GoToState`), with setters and storyboards that run over time.

**Drawing.** Direct2D and DirectWrite through DXVK, with GDI as a fallback: shapes, paths and geometries, solid, gradient and image brushes (`AcrylicBrush` as its fallback colour), corner radii, opacity, render transforms and clips. Fonts include the ones an app ships in its package, and icon glyphs fall back to Segoe Fluent Icons or Segoe MDL2 Assets. Light and dark themes, including changes while the app runs.

**Input.** Mouse, keyboard focus with Tab navigation and a focus visual, routed key and focus events, `FocusManager`, `VisualTreeHelper` and `AutomationProperties`.

**Not yet:** WinUI 2 controls (the `Microsoft.UI.Xaml` library Calculator and Solitaire Collection use), menus (`MenuFlyout`), list virtualization, grouping and drag and drop, IME input and undo in text boxes, transitions, automation peers (Narrator), SVG images, and composition brushes such as `XamlCompositionBrushBase`.

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
| `UWP_WRAPPER="mangohud gamemoderun"` | A command to start the app's Wine through, such as MangoHud or GameMode |
| `UWP_LAUNCHER_EXEC=...` | The command launchers made by `setup-prefix.sh` run, followed by the package name (default: this repo's `run-app.sh`); UWP Launcher sets it to its own `--run` |
| `UWP_PREFIX`, `UWP_DATA_DIR`, `UWP_LOGS`, `UWP_BUILD_DIR` | Move the shared prefix, data, logs or build folders |
| `UWP_RELEASE_REPO` | GitHub `owner/repo` that `fetch-release.sh` downloads from (default: this project) |
| `GE_PROTON` | Path to GE-Proton 11-14 when building from source, if it isn't found automatically |

To set any of these for one app only, put `export` lines in `~/.local/share/wine-uwp/apps/<package name>.env` (or `<package name>@<slot>.env` for a [version kept apart](#set-up-an-app)): `run-app.sh` reads it after the repo's [`apps/<package name>.conf`](apps/README.md), so it wins, and launchers use it too. For example `export WINE_CORE_WINDOWED=1`.

## Saves and settings

Local files and settings are written where Windows would put them, inside the prefix: `drive_c/users/<you>/AppData/Local/Packages/<package family name>/` (`LocalState` for the app's files, `Settings` for its settings store). Xbox connected storage is kept there too, under `LocalState/XboxGameSaves/<service config id>/<container>/<blob>`, because there is no Xbox Live to sync it to. They persist between runs; back that folder up if you care about it.

## Limitations

- **Offline only.** You are signed in as your Linux user name; there is no Xbox Live. Multiplayer and LAN may work for some games.
- **No add-ons.** Wine has no Store client, so only the app's own licence is reported; add-ons and DLC that require a licence are unavailable.
- **Xbox cloud saves stay on this PC.** Nothing is synchronised with Xbox Live, so saves from Windows or Xbox do not appear, and the reverse.
- **The screen can still blank.** `DisplayRequest` asks Wine to keep the display on, but Wine 11.22 does not pass that on to the desktop, so a screensaver or display power-off may still happen.
- **Fullscreen or windowed is chosen at launch.** The window is created borderless at your screen's resolution, or as a 1280x720 window with a title bar with `WINE_CORE_WINDOWED=1` or when the app last asked to launch windowed (its `PreferredLaunchWindowingMode`, which casual games such as Minesweeper set; it is kept per app in `HKCU\Software\Wine\WindowsRuntime\Packages\<package name>`). Its style is not changed while you play, because resizing a mapped window under its Vulkan swapchain can leave it without mouse input under XWayland. An app's own windowed-mode option may lose the mouse the same way; set `WINE_CORE_WINDOWED=1` instead.
- **Resource strings.** `ms-resource:` names in the manifest are not resolved (no `resources.pri` support), so an app with such a display name gets its package name as the window title.
- **D3D12 and D3D11 only.** The graphics setup covers D3D12 (vkd3d-proton) and D3D11 (DXVK, tested with Minecraft and the Unity games); other renderers on a CoreWindow are untested.

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

Each run writes `~/.local/share/wine-uwp/logs/run-<package>-<time>.log` (`run-<package>@<slot>-<time>.log` for a version kept apart; the last 8 of each are kept, and `latest.log` points at the newest). The last line says how the app exited. If it crashed, a backtrace is saved next to it as `run-<package>-<time>.crash.txt`. Include both when reporting a problem.

## Publishing a release

For maintainers. After building (see above) and committing, with GE-Proton 11-14 installed:

```bash
scripts/build-wine.sh
scripts/package-release.sh
gh release create 11.22-uwp5 --title wine-11.22-uwp5 dist/wine-uwp-x86_64.tar.xz dist/wine-uwp-x86_64.tar.xz.sha256 --notes-file dist/NOTES.md
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
                ConnectionCost (Minecraft disabled multiplayer while it failed), 0117 publisher storage for the
                Microsoft casual games: GetPublisherCacheFolder subfolders ("mcg\analytics"), CreateFolderAsync and
                CreateFileAsync make missing parent folders, GetFoldersAsync/GetFilesAsync/GetItemsAsync, and
                IStorageItemProperties (launch data such as mcg\user_data\Guest\launch_data was never written), 0118
                IStorageFolder2.TryGetItemAsync (their ad-consent files), GetItemAsync and deleting a folder
patches/dxvk/   patches against DXVK f90f1bf1 (v3.1.1-235): CoreWindow and composition swap chains
docker/         build containers
apps/           optional per-app settings
scripts/        fetch-release.sh, build-wine.sh, build-dxvk.sh, setup-prefix.sh, run-app.sh, list-apps.sh,
                remove-app.sh, package-release.sh, uwptool.py, winmd2idl.py (WinRT IDL from Windows metadata),
                xbf.py (dump compiled XAML, or compile test XAML)
tests/xaml/     the XAML test clients (build.sh, run.sh): xamlhello (milestone 1), xamllayout (milestone 2),
                xamlmarkup + markup/ (milestones 3 and 4)
tests/scripts/  run.sh: version slots, list-apps.sh and remove-app.sh in a throwaway prefix
```
