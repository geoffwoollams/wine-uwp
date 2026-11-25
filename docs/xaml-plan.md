# Windows.UI.Xaml in wine-uwp: plan

This replaces `docs/xaml-scoping.md` (commit 2c84be6), which concluded that XAML support was "not feasible in reasonable time" and added no code. Some of that document holds up and some doesn't; section 2 goes through it claim by claim. Short version:

- **Supporting real Store XAML apps in general is a UI-framework-sized job.** That much is true, and nobody should expect parity with Windows.
- **A staged route that runs real, simple XAML apps is feasible.** It's a C implementation of a growing XAML subset inside Wine's own `windows.ui.xaml.dll`. Its rungs are small and independently testable, and Microsoft's now open-source (MIT) WinUI 3 code is the behavioural reference and the source of the theme and template markup.
- **Milestone 1 fits in one session**: a code-only XAML app (no markup) reaching `OnLaunched` and showing a `TextBlock` in our CoreWindow.

Recommendation: **route A** (section 4). Write a native C subset in `dlls/windows.ui.xaml`, rendered with GDI first and Direct2D/DirectWrite later. Use the MIT WinUI 3 sources for algorithms, the XBF format and the default templates. Do not try to host WinUI 3 wholesale, bridge to Uno, or load Microsoft's own `Windows.UI.Xaml.dll`.

---

## 1. What a XAML UWP app actually does at startup

Below is the path a Visual Studio "Blank App (Universal Windows)" takes, C++/CX or C#. Each step is what our implementation has to satisfy, in order. A C# app built with .NET Native compiles to the same WinRT calls; it also needs the `Microsoft.NET.Native.Framework/Runtime` framework packages, which the user supplies the same way as `Microsoft.VCLibs`.

1. **Activation.** The manifest has `<Application Executable="App.exe" EntryPoint="App.App">`. Nothing XAML-specific happens in the activation itself: `run-app.sh` starts the exe directly, as for Halo Wars 2. The XAML runtime classes must be activatable through `RoGetActivationFactory`. Wine does that from `HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId`, written by `classes.idl` with `#pragma makedep register`, the same way patch 0011 registers `CoreWindow`.
2. **`main` → `Application.Start(callback)`.** The generated `main` (in `App.g.hpp`, or `App.g.i.cs`) calls `IApplicationStatics::Start` with an `ApplicationInitializationCallback`. `Start` belongs to `Windows.UI.Xaml.dll`. It calls `CoreApplication.Run` with its own `IFrameworkViewSource`, and our host (`dlls/twinapi.appcore/core_gen.c`, `core_app_Run`) then calls `Initialize`, `SetWindow`, `Load`, raises `Activated`, then calls `Run`.
3. **The callback constructs `App`.** `App` derives from `Windows.UI.Xaml.Application`, so its constructor calls `IApplicationFactory::CreateInstance(outer, &inner, &instance)`. That is **WinRT aggregation**: our object becomes the inner object of the app's own object. The app's overrides (`OnLaunched`, `OnActivated`, ...) are reached by querying the outer object for `IApplicationOverrides`.
4. **`App::InitializeComponent`** calls `Application.LoadComponent(this, "ms-appx:///App.xaml")`, which loads the compiled `App.xbf` from the package root. The template also subscribes `Application.Suspending` (and `UnhandledException` in Debug builds).
5. **Activation → `OnLaunched(LaunchActivatedEventArgs)`.** The template code does this:
   ```
   rootFrame = Window.Current.Content as Frame  (null on first launch)
   rootFrame = new Frame(); Window.Current.Content = rootFrame
   rootFrame.Navigate(typeof(MainPage), e.Arguments)
   Window.Current.Activate()
   ```
6. **`Frame.Navigate(TypeName)`** asks the app's generated `IXamlMetadataProvider` (implemented by `App`, found by QI on the Application's outer object) for the `IXamlType` of `MainPage`, then calls `ActivateInstance`.
7. **`MainPage` constructor → `InitializeComponent` → `LoadComponent("ms-appx:///MainPage.xaml")`.** This loads `MainPage.xbf` (XBF v2), creates the built-in objects it names (`Grid`, `TextBlock`, `Button`, ...), sets their properties, and resolves `{StaticResource}`/`{ThemeResource}` against app and system resources. It then calls back into the page's generated `IComponentConnector::Connect(id, target)` for every `x:Name` and event handler.
8. **Render loop.** `FrameworkView.Run` calls `CoreDispatcher.ProcessEvents(ProcessUntilQuit)`. XAML lays out (Measure/Arrange) and renders when the tree is dirty, and routes pointer and keyboard input through hit-testing and focus.

So the smallest real app needs steps 2 to 8. The smallest *test* app, one written against the ABI with no markup, needs only steps 2, 3, 5 (with a `TextBlock` instead of a `Frame`) and 8. That gap is why milestone 1 is code-only and markup comes in milestone 3.

## 2. The earlier scoping document, claim by claim

| Claim in `xaml-scoping.md` | Verdict | Evidence |
|---|---|---|
| Wine 11.0 `windows.ui.xaml.dll` only has `ColorHelper`; the IDL only covers `DependencyObject`, `DependencyProperty`, `PropertyMetadata`, `DataTemplateKey` | **True** | `dlls/windows.ui.xaml/` is about 300 lines; `include/windows.ui.xaml.idl` is 256 lines. `include/windows.ui.composition.idl` exists (192 interfaces) but has no implementation. |
| GE-Proton 11-14 ships no XAML | **True** | Its `windows.ui.xaml.dll` is the same Wine stub (111 KB, `ColorHelper` only). |
| Upstream Wine has no XAML implementation or plan | **True as of Nov 2026** | WineHQ bugs (for example 57668, 3D Builder failing on `Windows.UI.Xaml.PropertyMetadata`) still describe XAML as missing; only headers (MR 2959, 3437) and stubs landed. |
| Default control templates and `generic.xaml` live in Microsoft's closed `Windows.UI.Xaml.dll` and can't be redistributed | **Outdated, and the most important correction** | Microsoft has published the WinUI 3 framework source in [microsoft/microsoft-ui-xaml](https://github.com/microsoft/microsoft-ui-xaml) under **MIT** (main branch, about 29,000 files, about 4,400 `.cpp`). WinUI 3 is the lifted fork of system XAML. The repo contains `dxaml/xcp/dxaml/themes/generic.xaml`, the `*_themeresources.xaml` files, the XBF reader and writer (`dxaml/xcp/core/Parser`, `dxaml/xcp/tools/XbfParser`), the layout engine, controls, text and focus code. MIT is compatible with Wine's LGPL, so the theme markup and templates can be adapted, with attribution. |
| Apps ship `.xbf` and need an XBF parser and the app-generated metadata provider | **True, but bounded** | XBF v2 is a binary node stream with string, type and property tables. The MIT parser shows the format exactly. The metadata provider is the app's own code: we only *call* it through `IXamlMetadataProvider`/`IXamlType`, which are about 20 methods. |
| Hundreds of IDL interfaces with exact IIDs and vtable order | **True, but mechanical** | The IIDs and method order come from Windows metadata (`Windows.winmd`). [microsoft/windows-rs](https://github.com/microsoft/windows-rs) (MIT/Apache-2.0) carries a full copy at `crates/libs/default/Windows.winmd`. A small script can turn it into widl IDL, the way the existing Wine IDL was written. Only the interfaces each milestone touches need to be added. |
| A thin subset would just show a blank window | **Partly true** | True if the subset stops at hosting, which is why milestone 1's test checks that real text is drawn. It is not true that a subset can never run real apps: a Blank App template with a few controls only touches about 15 runtime classes. |
| No cross toolchain with WinRT XAML headers, so nothing could be tested | **Fixable** | The build container already cross-compiles PE code with MinGW. A test app in C, written against Wine's widl-generated headers (as Wine's own `tests/` do), exercises the same ABI a C++/CX app uses, as long as the IIDs are the real ones. |
| Multiple person-months for a subset that runs a simple real app | **About right** | See the ladder: milestones 1 to 3 (a VS Blank App with a few controls) come to roughly 2 to 4 months of focused work. |

## 3. What exists to reuse

| Source | What is useful | Licence | Use |
|---|---|---|---|
| Wine 11.0 `windows.ui.xaml` | Module, `DllGetActivationFactory`, class registration, `DependencyObject` IDL | LGPL | Extend in place |
| Our `twinapi.appcore` host | `CoreApplication.Run` → `IFrameworkView` sequence, `CoreWindow`, `CoreDispatcher.RunAsync`, input events, `ICoreWindowInterop` (HWND) | LGPL | XAML's `FrameworkView` plugs into it unchanged; `ProcessEvents(ProcessUntilQuit)` needs a real blocking loop |
| Wine `d2d1`, `dwrite`, `windowscodecs`, `gdi32` | 2-D drawing, text layout and shaping, image decoding | LGPL | GDI for milestone 1 (simplest, robust in Wine); D2D/DirectWrite from milestone 2 (needed for anti-aliased text, transforms, opacity) |
| Wine `dcomp`, `windows.ui.composition.idl` | Only stubs and headers | LGPL | Not needed until apps use `ElementCompositionPreview`/Composition APIs (milestone 6) |
| **microsoft-ui-xaml (WinUI 3) source** | XBF v2 format and reader, layout algorithms (Grid star sizing, StackPanel, ScrollViewer), the DP precedence rules, `generic.xaml` and theme resources, control templates, focus and keyboard navigation rules | **MIT** | Reference for behaviour and format; adapt the theme XAML. Not built as-is (see route B) |
| **WinUI 2** (`microsoft-ui-xaml` winui2 branch) | Source of the `Microsoft.UI.Xaml.2.x` controls many UWP apps ship as a framework package | MIT | Apps bring the binary in their own framework package; it runs on top of system XAML, so it needs a fairly complete system XAML underneath (milestone 6) |
| **Uno Platform** | A C# re-implementation of the WinUI API surface | Apache-2.0 | Not ABI-compatible (managed objects, no WinRT vtables), and native C++/CX apps can't call into it. Useful only as a second behavioural reference |
| windows-rs `Windows.winmd` | Exact IIDs and method order for every `Windows.UI.Xaml` interface | MIT / Apache-2.0 | Generate IDL from it |
| Microsoft's own `Windows.UI.Xaml.dll` (from a user's Windows install) | The real thing | Proprietary OS component; can't be redistributed | Rejected: see route D |

## 4. Routes, weighed

**A. Native C subset in Wine's `windows.ui.xaml.dll` (recommended).** We write the object model, a dependency-property store, layout, a retained element tree and a renderer in C, growing by milestone. Behaviour follows the MIT WinUI 3 sources; the default styles are adapted from their `generic.xaml` once templates exist.
- *For:* fits the existing patch workflow and Wine's code style, so it could go upstream. Each rung is testable on its own. No new runtime dependencies. We only implement what apps actually call.
- *Against:* every control is our own code; fidelity drifts from Windows, and the long tail never ends.
- *Effort:* see the ladder. *Risk:* medium. The hard parts (templates, `ListView` virtualization, text input) come late and can be stopped at any rung with something useful already working.

**B. Port WinUI 3 (MIT) to back `Windows.UI.Xaml`.**
- *For:* real fidelity: the same layout, templates and controls as Windows.
- *Against:*
  - It depends on closed binaries: the lifted IXP package (`Microsoft.UI.Composition`, `Microsoft.UI.Input`, content islands) and `Microsoft.FrameworkUdk`. Both appear in `Packages.props` and neither is open source, so a compositor and input layer would have to be written first.
  - It builds with MSVC, WRL/WIL and an internal-style build system, not MinGW/widl.
  - Its ABI is `Microsoft.UI.Xaml`, with different IIDs and flattened interfaces (system XAML has `IUIElement` … `IUIElement10`), so an IID and vtable translation layer would sit over about 1,500 interfaces.
- *Effort:* many person-months before the first pixel. *Risk:* very high. Rejected as the main route; kept as a reference.

**C. Bridge to another toolkit (Uno, GTK, Qt).** The app calls WinRT vtables on objects that must be real XAML objects: aggregation, `DependencyObject` identity, the metadata provider, XBF semantics. A bridge would have to re-implement all of that anyway, then translate it into a second toolkit's object model. It is more work than route A, with worse fidelity. Rejected.

**D. Run Microsoft's own `Windows.UI.Xaml.dll` as a native override, supplied by the user like VCLibs.** It depends on the Windows composition stack: `dcomp`/`Windows.UI.Composition` talking to DWM's `dwmcore`, `CoreMessaging`, `twinapi`, private `Windows.UI.dll` interfaces, and `CoreUIComponents`. Wine has none of these, and they are far larger than XAML itself. Users would also have to copy OS binaries out of a Windows install, which is less clear-cut than a redistributable framework package. Rejected.

## 5. Architecture for route A

- **Module:** `dlls/windows.ui.xaml`, with a new `IMPORTS` of `user32 gdi32` (later `d2d1 dwrite windowscodecs`).
- **IDL:** grow `include/windows.ui.xaml.idl` and add `windows.ui.xaml.controls.idl` and `windows.ui.xaml.media.idl` as milestones need them, generated from `Windows.winmd` so the IIDs and vtable order are exact. Contract versions follow the metadata.
- **Object model:** one C struct per runtime class, with base-class parts embedded (`struct uielement` inside `struct framework_element` inside `struct text_block`), and a per-class vtable of internal hooks (`measure`, `arrange`, `render`, `hit_test`). The public WinRT interfaces are thin wrappers. Composable classes (`Application`, `Page`, `UserControl`, `Panel`, `Control`) follow COM aggregation: the inner object's non-delegating `IInspectable`; overrides are found through `QueryInterface` on the outer object.
- **Dependency properties:** a per-object sorted array of `(property id, value as IInspectable/PropertyValue)`, plus defaults and inherited values (`FontSize`, `Foreground`, `FlowDirection`, `DataContext`). Change callbacks invalidate measure, arrange or render. The precedence rules (local > template > style > inherited > default) follow WinUI's `DependencyObject` code.
- **Hosting:** `Application.Start` gives `CoreApplication.Run` our own `IFrameworkViewSource`. `FrameworkView.SetWindow` creates the per-thread `Window` around the `CoreWindow`, gets its HWND through `ICoreWindowInterop`, and subclasses it for `WM_PAINT`/`WM_SIZE`. Input comes from the `CoreWindow` pointer and key events. `Run` raises `OnLaunched` and pumps messages until `WM_QUIT`.
- **Rendering:** milestone 1 uses a GDI double-buffer on `WM_PAINT`. From milestone 2 it moves to a D2D HWND render target with DirectWrite text, the same tree walk with a different backend. A real compositor (`Windows.UI.Composition`) only arrives in milestone 6.
- **Markup:** an XBF v2 reader that builds objects directly, plus a small text-XAML reader for `XamlReader.Load` and our own embedded theme resources. Type resolution goes built-ins first, then the app's `IXamlMetadataProvider`.

## 6. Milestone ladder

Estimates are for one experienced Wine developer working full time, to "works for the test app", not to upstream quality. Each rung needs the previous ones.

| # | Milestone | Needs | Test app | Estimate |
|---|---|---|---|---|
| **1** | **Code-only app shows text.** `Application.Start`, `Application` aggregation, `OnLaunched` with `LaunchActivatedEventArgs`, `Application.Current`, `Window.Current`/`Content`/`Activate`/`Bounds`/`CoreWindow`/`Dispatcher`, `TextBlock` (`Text`, `FontSize`, `Foreground`), `SolidColorBrush`, `Application.Suspending` (registration only), window background, centred or top-left text, repaint on resize, clean exit on close. GDI rendering. | IDL for about 12 interfaces; blocking `ProcessEvents(ProcessUntilQuit)` | Our own C test app (MinGW), aggregating `Application` exactly as C++/CX does; checks a pixel and the text to prove it drew | 1 to 2 weeks |
| 2 | **Layout and a button.** `UIElement`/`FrameworkElement` Measure/Arrange (`Width`/`Height`/`Margin`/`HorizontalAlignment`/`VerticalAlignment`), `StackPanel`, `Grid` (rows, columns, star sizing), `Border`, `Button` with a built-in look and `Click`, pointer hit-testing and routing, `Visibility`. D2D/DirectWrite rendering. | DP store with change invalidation; `RoutedEventHandler`; `CoreWindow` pointer events | C test app: a Grid with a StackPanel of TextBlocks and a Button whose Click changes text | 3 to 5 weeks |
| 3 | **Real Blank App with markup.** XBF v2 reader, `Application.LoadComponent`, the `IXamlMetadataProvider`/`IXamlType`/`IXamlMember` client side, `IComponentConnector` (x:Name, Click handlers), `Frame` + `Page` + `Navigate(TypeName)`, `ms-appx:///` URIs, `Application.Resources` with `StaticResource` for brushes and numbers, `RequestedTheme` | MIT XBF parser as reference | A VS "Blank App (C++/CX)" with a TextBlock and Button page, built once on Windows; the `.appx` is added to this repo's test data only if its licence allows, otherwise the build steps are documented | 4 to 8 weeks |
| 4 | **Styles and templates.** `Style`/`Setter`/`BasedOn`, `ControlTemplate`, `ContentPresenter`/`ContentControl`, `TemplateBinding`, `VisualStateManager` (states without animation first), `ThemeResource` with light and dark themes, a built-in `generic.xaml` adapted from WinUI 3 (MIT) for Button, CheckBox, RadioButton, ToggleSwitch, Slider, ProgressBar, `{Binding}` with `INotifyPropertyChanged`, `x:Bind` (compiled into the app, so it only needs property-change plumbing) | Milestone 3 | Several sample apps from Microsoft's MIT [Windows-universal-samples](https://github.com/microsoft/Windows-universal-samples) (for example "XamlUIBasics" pages) | 3 to 6 months |
| 5 | **Everyday app controls.** `ScrollViewer` (wheel, touchpad), `TextBox`/`PasswordBox` (caret, selection, clipboard, IME through `Windows.UI.Text.Core`), `ListView`/`GridView`/`ItemsControl` with `DataTemplate` and virtualization, `Image` (WIC, `BitmapImage` from `ms-appx`), `ContentDialog`, `CommandBar`/`AppBarButton`, `SymbolIcon` (needs the Segoe MDL2 glyphs: a font the user must supply, or an open substitute), keyboard focus and Tab navigation | Milestone 4 | Simple open-source UWP utilities (calculator-style or notes-style apps) | 4 to 8 months |
| 6 | **The long tail.** `Windows.UI.Composition` and `ElementCompositionPreview`, Storyboard and theme animations, acrylic/reveal, WinUI 2 (`Microsoft.UI.Xaml.2.x` framework packages), .NET Native apps at scale, `MediaPlayerElement`, `WebView` (needs a browser engine), `InkCanvas`, accessibility (UIA), `resources.pri` lookups for localised strings and scale-specific images | Milestone 5 | Real Store apps, one at a time | Open-ended (a year or more) |

**Where feasibility ends.**
- Milestones 1 and 2 are solid engineering with known answers.
- Milestone 3 is where real, unmodified apps first start. It is bounded, because the XBF format is documented by Microsoft's own open code.
- Milestone 4 turns this into a UI framework. It is still feasible, and it is the minimum before ordinary apps look right, since nearly every control is drawn from a template.
- Milestone 5 is the point where a meaningful fraction of small Store apps could be usable.
- Milestone 6 has no end. Apps that lean on WinUI 2, Composition effects, `WebView` or media will stay broken for a long time. Parity with Windows is not a realistic goal for this project. "Simple utility and game-launcher-style XAML apps run" is.

Games are a special case: many UWP games use XAML only for a splash, a `SwapChainPanel` or a `SwapChainBackgroundPanel` hosting D3D. After milestone 2, `SwapChainPanel` plus `ISwapChainPanelNative::SetSwapChain` (which needs `CreateSwapChainForComposition` added to our DXVK `dxgi.dll`, the same way `CreateSwapChainForCoreWindow` was) is a cheap, high-value detour, about 2 to 3 weeks, worth doing before milestone 4 if a target game needs it.

## 7. Milestone 1 in detail (implemented in this repository)

Scope: exactly the first row of the table. The interfaces involved, with IIDs taken from `Windows.winmd`:

- `Windows.UI.Xaml`:
  - `IApplication`, `IApplicationStatics` (`Current`, `Start`, `LoadComponent`, `LoadComponentWithResourceLocation`), `IApplicationFactory`, `IApplicationOverrides`, `ApplicationInitializationCallback`, `IApplicationInitializationCallbackParams`
  - `IWindow`, `IWindowStatics`
  - `IUIElement`, `IFrameworkElement`, `IDependencyObject` (exists)
  - `SuspendingEventHandler`, `UnhandledExceptionEventHandler`
- `Windows.UI.Xaml.Controls`: `ITextBlock` (activated through `IActivationFactory::ActivateInstance`)
- `Windows.UI.Xaml.Media`: `IBrush`, `ISolidColorBrush`, `ISolidColorBrushFactory`

Anything else on those interfaces returns `E_NOTIMPL` with a `FIXME`. `LoadComponent` returns `E_NOTIMPL` until milestone 3. That is deliberate: a real Blank App fails right there, in a clearly logged way.

Out of scope for milestone 1: layout beyond a single content element, any input other than close, XBF, and every other control.

### Status: milestone 1 implemented (patches 0013-0016)

- **0013 widl:** writing `.winmd` crashed on a runtime class whose interface list includes an instantiated generic (for example `ResourceDictionary` : `IMap<...>`). It now skips the missing contract attribute instead of dereferencing NULL.
- **0014 include:** about 5,000 lines of IDL generated by `scripts/winmd2idl.py` from `Windows.winmd`. It fully defines `Application`, `ApplicationInitializationCallbackParams`, `Window`, `UIElement`, `FrameworkElement`, `Controls.TextBlock`, `Media.Brush` and `Media.SolidColorBrush`, plus about 200 interfaces, enums, structs and delegates they reference. All 185 non-generic interface IIDs in the generated header were checked against `Windows.winmd`, and all match.
- **0015 twinapi.appcore:** `CoreDispatcher.ProcessEvents(ProcessUntilQuit)` now blocks in a `GetMessage` loop and runs `RunAsync` work. The other options are unchanged; Halo Wars 2 uses `ProcessAllIfPresent`.
- **0016 windows.ui.xaml:** `application.c` (statics, aggregation, `FrameworkView`/`FrameworkViewSource`, the activation handler), `window.c` (`Window` over the CoreWindow HWND, subclassed for `WM_PAINT`), `element.c` (the shared `UIElement`/`FrameworkElement`/`DependencyObject` part and single-element layout), `text_block.c` and `brush.c`. The four classes are registered at wineboot through `classes.idl`.

**Tests run** (Linux, Wine built by `scripts/build-wine.sh`, fresh scratch prefixes):

- `tests/xaml/xamlhello.c`, a MinGW-built C client that behaves like a C++/CX `App`: **34/34 checks pass**. It covers:
  - aggregation identity and `IApplicationOverrides` coming from the outer object
  - `OnLaunched` with launch args, `Application.Current`, `Window.Current`, `Content` round-trip, `Bounds` 1280x720
  - `RunAsync` work running inside the XAML message loop
  - a pixel check: red 36 px text drawn centred on a white background
  - clean exit through `Application.Exit`, with the App object released
- **Halo Wars 2** (not a XAML app), 90 seconds windowed from launch, compared with the v1.0 release build in a second scratch prefix: identical distinct `err`/`fixme` messages, about the same log volume (11,287 against 11,299 lines), and the game still in its normal `ProcessEvents` render loop when the time limit ended it. Gameplay and input were not exercised.

**Not tested or not working:**

- No real Store XAML app was run, because none is available here, and any VS-built app would stop at `Application.LoadComponent` (`.xbf`), which returns `E_NOTIMPL` until milestone 3. The C test exercises the same ABI calls, with real IIDs, that compiled C++/CX code makes, but not the C++/CX runtime (`vccorlib`) itself.
- The VS template's `OnLaunched` reads `e->PrelaunchActivated` (`ILaunchActivatedEventArgs2`), which the host's activation args don't implement yet. The template also uses `Frame`, `Page`, `Application.Resources` and `DebugSettings`, all of which are milestone 3.
- `Window.Activated`/`SizeChanged`/`Closed`/`VisibilityChanged` and `Application.Suspending`/`Resuming`/`UnhandledException` handlers are stored but never raised.
- `UIElement.Opacity` is stored but not drawn. A brush whose alpha is 0 hides the text, but partial transparency is drawn opaque (GDI). There is no hit-testing or input routing.
- Fonts: XAML asks for "Segoe UI"; without it installed, GDI substitutes a sans-serif face, so metrics differ from Windows.
- Only fresh prefixes were tested. An existing prefix gets the new class registrations when Wine updates it on the next start (`wine.inf` changes on install), or after `wineboot -u`.
- Found in passing, not changed: the host's `ICoreWindowInterop` (and the dxgi/DXVK patches that call it) uses an IInspectable-shaped vtable with `get_WindowHandle` at slot 6. The real interface derives from IUnknown (slot 3), so third-party code using the real layout would get the wrong method. XAML finds the HWND by window class so it doesn't depend on this.
