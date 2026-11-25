# wine-uwp

Run unpacked **UWP (Microsoft Store) apps and games** on Linux with a patched Wine 11.0.

UWP apps don't create a normal Win32 window. They ask Windows for a `CoreApplication`, get a `CoreWindow`, and read their identity, storage, licence and sign-in from WinRT classes that Wine 11.0 mostly lacks. This project adds the missing pieces, without touching the app's files:

- a `CoreApplication`/`CoreWindow` host (window, mouse and keyboard input, fullscreen, lifecycle)
- package identity read from the app's `AppxManifest.xml`
- `ApplicationData` (local files and settings), `GameSaveProvider` (Xbox connected storage kept on disk), `DisplayRequest`
- an offline Xbox sign-in for your Linux user, and an offline Store licence for the app itself (`StoreContext` and the older `CurrentApp`)
- a DXVK `dxgi.dll` with `CreateSwapChainForCoreWindow`, so D3D12 apps can draw into the CoreWindow

**Tested apps:**

| App | Status |
|---|---|
| Halo Wars 2 (`Microsoft.HoganThreshold`, D3D12) | Playable offline: videos, menus, skirmish, native-resolution fullscreen, mouse and keyboard, settings and local saves. See [the example](#example-halo-wars-2). |

Other native C++ UWP apps may work; many will need WinRT classes this project doesn't have yet. Real XAML apps (anything built with `Windows.UI.Xaml` pages and markup) do not work yet; see [XAML apps](#xaml-apps). Reports and patches are welcome.

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

Apps built with C++/CX also need the **x64 `Microsoft.VCLibs.140.00`** framework package (the UWP one, not the `UWPDesktop` one), either as a `.appx` file or as the installed folder, for example `C:\Program Files\WindowsApps\Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe` from a Windows install. If the package has a `redist/` folder containing that `.appx`, it is found automatically.

```bash
scripts/setup-prefix.sh "/path/to/package-folder"
# or, with the VC++ package elsewhere:
scripts/setup-prefix.sh "/path/to/package-folder" --vclibs /path/to/Microsoft.VCLibs.140.00_..._x64.appx
```

This checks every file against the hashes in `AppxBlockMap.xml` (a few minutes for a big app; `--skip-verify` skips it), creates the shared Wine prefix on first use, and links the package into it as `C:\UWP\<package name>`. The package folder is never modified. Run it again for each further app; they share the prefix.

Play:

```bash
scripts/run-app.sh                       # if only one app is set up
scripts/run-app.sh Microsoft.HoganThreshold
```

Everything lives under `~/.local/share/wine-uwp/` (the prefix and logs) and the repo's `build/` folder. Delete those to uninstall.

## XAML apps

Wine has no `Windows.UI.Xaml`, the UI framework most non-game UWP apps are built on. This project is adding it in stages; [docs/xaml-plan.md](docs/xaml-plan.md) has the plan, the evidence behind it and estimates.

**Milestone 1 (done): code-only XAML apps.** `Application.Start`, an app's own `App` class deriving from `Application` (with `OnLaunched` called on launch), `Application.Current`, `RequestedTheme` (light or dark background), `Window.Current` (`Content`, `Activate`, `Bounds`, `Dispatcher`, `Close`), and a `TextBlock` (`Text`, `FontSize`, `FontWeight`, `FontStyle`, `Foreground`, `TextWrapping`, `TextAlignment`, `Padding`) with `Margin`, `Width`/`Height` and alignment, drawn in the CoreWindow. Brushes are `SolidColorBrush` only. Rendering is GDI, so text is anti-aliased but there are no transforms or opacity yet.

**Milestone 2 (done): layout and buttons.** Measure/arrange layout with `StackPanel`, `Grid` (Auto, pixel and star rows and columns, spans), `Border`, and `Button` with the Windows 10 look, hover and pressed states, and `Click` from the mouse. Elements can be subclassed by apps (aggregation), and `Panel.Children` and Grid row/column definitions are real collections.

**Milestone 3 (done): compiled markup.** `Application.LoadComponent` loads the app's `.xbf` files (compiled XAML, version 2), so an app laid out like Visual Studio's Blank App starts. The markup can use the controls above, `Page`, `UserControl` and `Frame` (with `Navigate` creating the app's pages through its `IXamlMetadataProvider`), `x:Name` and `Click` handlers, `Application.Resources` and `{StaticResource}`, and common `{ThemeResource}` brushes. Microsoft's own compiler-made test files load.

**Milestone 4, part 1 (done): styles.** Explicit and implicit `Style`s with `BasedOn` (local values still win), theme dictionaries for `{ThemeResource}`, merged dictionaries and `ResourceDictionary Source="ms-appx:///Styles.xaml"`.

**Milestone 4, part 2 (done): control templates.** `ControlTemplate` (directly or from a style), `TemplateBinding`, `ContentPresenter`, and `VisualStateManager` states with setters and storyboards (which jump to their final values), so a templated Button changes look on hover and press.

**Not yet:** the built-in controls' default templates, animation timing, data templates, data binding (`{Binding}`), `TextBox`, scrolling, keyboard focus, localized `x:Uid` strings and navigation history. Real Store apps will mostly still look wrong or stop where they need these; see [docs/xaml-plan.md](docs/xaml-plan.md).

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
| `WINE_REPORT_HOSTNAMES=1` | Report the PC's real IP addresses to the app (off by default) |
| `WINEDEBUG=-all` | Turn off logging (default logs errors, fixmes and the WinRT channel) |
| `UWP_PREFIX`, `UWP_DATA_DIR`, `UWP_LOGS`, `UWP_BUILD_DIR` | Move the prefix, data, logs or build folders |
| `UWP_RELEASE_REPO` | GitHub `owner/repo` that `fetch-release.sh` downloads from (default: this project) |
| `GE_PROTON` | Path to GE-Proton 11-14 when building from source, if it isn't found automatically |

## Saves and settings

Local files and settings are written where Windows would put them, inside the prefix: `drive_c/users/<you>/AppData/Local/Packages/<package family name>/` (`LocalState` for the app's files, `Settings` for its settings store). Xbox connected storage is kept there too, under `LocalState/XboxGameSaves/<service config id>/<container>/<blob>`, because there is no Xbox Live to sync it to. They persist between runs; back that folder up if you care about it.

## Limitations

- **Offline only.** You are signed in as your Linux user name; there is no Xbox Live, multiplayer or LAN.
- **No add-ons.** Wine has no Store client, so only the app's own licence is reported; add-ons and DLC that require a licence are unavailable.
- **Xbox cloud saves stay on this PC.** Nothing is synchronised with Xbox Live, so saves from Windows or Xbox do not appear, and the reverse.
- **The screen can still blank.** `DisplayRequest` asks Wine to keep the display on, but Wine 11.0 does not pass that on to the desktop, so a screensaver or display power-off may still happen.
- **Fullscreen or windowed is chosen at launch.** The window is created borderless at your screen's resolution (or as a 1280x720 window with `WINE_CORE_WINDOWED=1`) and is not resized while you play, because resizing a mapped window under its Vulkan swapchain can leave it without mouse input under XWayland. An app's own windowed-mode option may lose the mouse the same way; set `WINE_CORE_WINDOWED=1` instead.
- **Resource strings.** `ms-resource:` names in the manifest are not resolved (no `resources.pri` support), so an app with such a display name gets its package name as the window title.
- **Only D3D12 apps are covered** by the graphics setup; D3D11 or other renderers on a CoreWindow are untested.

## Example: Halo Wars 2

Package `Microsoft.HoganThreshold` 1.11.2931.2, x64, DirectX 12, a native C++/CX game. The game files are used exactly as shipped; `setup-prefix.sh` verifies all of them against `AppxBlockMap.xml` and refuses to continue if any differ.

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

## Troubleshooting

Each run writes `~/.local/share/wine-uwp/logs/run-<package>-<time>.log` (the last 8 are kept; `latest.log` points at the newest). The last line says how the app exited. If it crashed, a backtrace is saved next to it as `run-<package>-<time>.crash.txt`. Include both when reporting a problem.

## Publishing a release

For maintainers. After building (see above) and committing, with GE-Proton 11-14 installed:

```bash
scripts/package-release.sh v1.0
gh release create v1.0 dist/wine-uwp-x86_64.tar.xz dist/wine-uwp-x86_64.tar.xz.sha256 --notes-file dist/NOTES.md
```

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
patches/wine/   patches against the wine-11.0 tag (apply in order with git apply): 0001-0007 the UWP host and
                services, 0008 CurrentApp, 0009 DisplayRequest, 0010 GameSaveProvider, 0011 class registration,
                0012 no hard-coded Store ID, 0013-0016 Windows.UI.Xaml milestone 1 (widl fix, generated IDL,
                ProcessUntilQuit, the XAML code), 0017-0018 milestone 2 (IDL, layout, panels, Button),
                0019-0022 milestone 3 (widl registration fix, IDL, launch args, markup loading),
                0023 milestone 4 part 1 (styles, theme and merged dictionaries), 0024-0025 part 2 (control
                templates, visual states)
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
