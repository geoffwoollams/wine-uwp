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

| App | Status |
|---|---|
| Halo Wars 2 (`Microsoft.HoganThreshold`, D3D12) | Playable offline: videos, menus, skirmish, native-resolution fullscreen, mouse and keyboard, settings and local saves. See [the example](#example-halo-wars-2). |
| Gears of War 4 (`Microsoft.SpartaUWP`, Unreal Engine 4, D3D12) | Campaign playable offline in fullscreen: menus, videos, mouse look and aiming, keyboard; the built-in benchmark runs. No multiplayer or co-op. See [the example](#example-gears-of-war-4). |

Other native C++ UWP apps may work; many will need WinRT classes this project doesn't have yet. XAML apps (built with `Windows.UI.Xaml` pages and markup) have partial support: simple ones start and work, real Store apps will often still stop or look wrong; see [XAML apps](#xaml-apps). Reports and patches are welcome.

## Quick start: prebuilt release

The [releases page](https://github.com/geoffwoollams/wine-uwp/releases) has the patched Wine, `dxgi.dll` and the D3D12/WinRT DLLs ready to use. No Docker, no compiling, no GE-Proton.

Requirements: x86-64 Linux with a Vulkan GPU and driver that vkd3d-proton supports (recent Mesa RADV/ANV or NVIDIA proprietary), `git`, `curl`, `python3`, and Wine's usual runtime libraries on the host (Vulkan loader, FreeType, X11/Wayland, PulseAudio/ALSA, GnuTLS). The release is built on Debian trixie, so a 2025 or newer distribution is best.

```bash
git clone https://github.com/geoffwoollams/wine-uwp.git
cd wine-uwp
scripts/fetch-release.sh
```

Then follow [Set up an app](#set-up-an-app).

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

You need the app as an **unpacked package folder**: the folder with `AppxManifest.xml`, `AppxBlockMap.xml` and the executable, for example copied from `C:\Program Files\WindowsApps\<package>` or extracted from the `.appx`/`.msix`. This project does not provide apps and does not bypass licensing: the app is treated as licensed because Wine has no Store client to ask, so you need to own it.

Apps built with C++/CX also need the **x64 `Microsoft.VCLibs.140.00`** framework package (the UWP one, not the `UWPDesktop` one), either as a `.appx` file or as the installed folder, for example `C:\Program Files\WindowsApps\Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe` from a Windows install. If the package has a `redist/` folder containing that `.appx`, it is found automatically. Most games ship one there: Halo Wars 2 and Gears of War 4 both do. If an app has none, the package found for an app you set up earlier is used. Microsoft doesn't offer the UWP `Microsoft.VCLibs.140.00` as a download of its own: its download page has only the `UWPDesktop` package, which doesn't work here. The UWP package comes with the Windows 10 SDK and Visual Studio (`ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64`), or is already installed on any Windows PC that has Store apps.

```bash
scripts/setup-prefix.sh "/path/to/package-folder"
# or, with the VC++ package elsewhere:
scripts/setup-prefix.sh "/path/to/package-folder" --vclibs /path/to/Microsoft.VCLibs.140.00_..._x64.appx
```

This creates the shared Wine prefix on first use, and links the package into it as `C:\UWP\<package name>`. The package folder is never modified. Run it again for each further app; they share the prefix. To give an app a prefix of its own instead, for example when it needs registry settings the others shouldn't see, add `--prefix /path/to/prefix`. That folder is remembered, so `run-app.sh`, launchers and later setups use it too. It can also be an existing prefix, such as one Lutris made, but wine-uwp's Wine upgrades it on first use and setup replaces its `dxgi`, D3D12 and WinRT DLLs, so the tool that made it may not run it properly afterwards. Add `--verify` to first check every file against the hashes in `AppxBlockMap.xml`, which catches a damaged or incomplete copy (a few minutes for a big app).

At the end it asks whether to **add a launcher** for the app: a `.desktop` file in your applications menu (`~/.local/share/applications/wine-uwp-<package name>.desktop`) and a copy on your desktop, with the app's own icon taken from its package. Pass `--shortcut` to add it without asking, or `--no-shortcut` to skip the question, for example in scripts. To remove a launcher, delete those two files.

Play, either from the launcher or from a terminal:

```bash
scripts/run-app.sh                       # if only one app is set up
scripts/run-app.sh Microsoft.HoganThreshold
```

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

Wine has no `Windows.UI.Xaml`, the UI framework most non-game UWP apps are built on. This project adds a **partial** implementation, in stages; what works so far is listed below, and [docs/xaml-plan.md](docs/xaml-plan.md) has the plan, the evidence behind it and estimates.

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

**Not yet:** ListView/GridView and other item controls, `x:Bind` beyond what its generated code calls, `TextBox`, scrolling, keyboard focus, localized `x:Uid` strings and navigation history. Real Store apps will still often stop or look wrong where they need these; see [docs/xaml-plan.md](docs/xaml-plan.md).

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
- **Fullscreen or windowed is chosen at launch.** The window is created borderless at your screen's resolution (or as a 1280x720 window with `WINE_CORE_WINDOWED=1`) and is not resized while you play, because resizing a mapped window under its Vulkan swapchain can leave it without mouse input under XWayland. An app's own windowed-mode option may lose the mouse the same way; set `WINE_CORE_WINDOWED=1` instead.
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
gh release create 11.22-uwp3.0 dist/wine-uwp-x86_64.tar.xz dist/wine-uwp-x86_64.tar.xz.sha256 --notes-file dist/NOTES.md
```

**Version numbers** look like `11.22-uwp3.0`: the Wine release the patches are based on, then wine-uwp's own version. Raise `UWP_VERSION` in `scripts/common.sh` for each GitHub release (`3.0`, `3.1`, ...) and change `WINE_TAG` when moving to a newer Wine; `RELEASE_VERSION` is made from both. `build-wine.sh` tags its source checkout so that `wine --version` reports `wine-11.22-uwp3.0`, and `package-release.sh` refuses a build whose version doesn't match. The download keeps the same file name, `wine-uwp-x86_64.tar.xz`, in every release, which is what lets `fetch-release.sh` find the latest one.

The archive contains the Wine install tree, `dxgi.dll`, the four component DLLs (`d3d12.dll`, `d3d12core.dll`, `wintypes.dll`, `windows.storage.dll`), the licence texts, `SOURCE.txt` (where to get every part's source) and a copy of `patches/`, `docker/` and `scripts/` (the recipe that built it). The script refuses to run with uncommitted changes in those folders, and refuses a Wine build containing add-on unlocking code.

## Licences

Everything in a release is free software, and a release contains no Microsoft software and no app files. The VC++ UWP runtime comes from your own `Microsoft.VCLibs` package at setup time and is never redistributed here.

| Part | Licence |
|---|---|
| Wine and the patches in `patches/wine/` | LGPL 2.1 or later |
| DXVK `dxgi.dll` and the patch in `patches/dxvk/` | zlib |
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
                on its window
patches/dxvk/   patch against DXVK f90f1bf1 (v3.1.1-235)
docker/         build containers
apps/           optional per-app settings
scripts/        fetch-release.sh, build-wine.sh, build-dxvk.sh, setup-prefix.sh, run-app.sh,
                package-release.sh, uwptool.py, winmd2idl.py (WinRT IDL from Windows metadata),
                xbf.py (dump compiled XAML, or compile test XAML)
tests/xaml/     the XAML test clients (build.sh, run.sh): xamlhello (milestone 1), xamllayout (milestone 2),
                xamlmarkup + markup/ (milestones 3 and 4)
docs/           xaml-plan.md
```
