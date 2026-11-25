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
- Found in passing, since fixed: the host's `ICoreWindowInterop` (and the dxgi/DXVK patches that call it) used an IInspectable-shaped vtable with `get_WindowHandle` at slot 6. The real interface derives from IUnknown (slot 3), so third-party code using the real layout would have called the wrong method. Patches 0002 and 0004 and the DXVK patch now use the IUnknown layout. XAML finds the HWND by window class, so it never depended on this.

## 8. Milestone 2: layout, panels, Border, Button and pointer input (patches 0017-0018)

**What was built:**

- **0017 include:** a further ~1,400 lines of generated IDL. It covers `Controls.Panel`, `StackPanel`, `Grid`, `Border`, `Button`, `Primitives.ButtonBase`, `ContentControl`, `Control`, `UIElementCollection`, `RowDefinition`/`ColumnDefinition` and their collections, and `RoutedEventArgs`. All 252 interface IIDs in the regenerated header match `Windows.winmd`.
- **0018 windows.ui.xaml:**
  - Every element can now be aggregated: the public interfaces delegate to an outer object, with a non-delegating inner. The composable factories (`IGridFactory`, `IStackPanelFactory`, `IButtonFactory`) accept an outer object, which `Page`/`UserControl` subclasses will need in milestone 3.
  - Layout is a real two-pass `Measure`/`Arrange` over a retained tree with parent links. It handles `Width`/`Height`, `Margin`, alignment and `Visibility.Collapsed`, and reports `ActualWidth`/`ActualHeight`, `DesiredSize` and `RenderSize`.
  - New elements:
    - `StackPanel`: vertical and horizontal.
    - `Grid`: Pixel, Auto and Star rows and columns, weighted stars, `Row`/`Column`/`RowSpan`/`ColumnSpan` through `IGridStatics`, and `ActualHeight`/`ActualWidth` on the definitions. Stars share the available space, but the Grid's desired size is its content, as in Windows.
    - `Border`: background, border, padding, child.
    - `Button`: string or element content, the Windows 10 light/dark Normal/PointerOver/Pressed/Disabled looks, padding 8,4,8,5, a 2 px border, `ClickMode`, `IsEnabled`, `IsPointerOver`/`IsPressed`, and `Click` with `RoutedEventArgs.OriginalSource`.
  - `Panel.Children` and the row/column collections share one `IVector`/`IVectorView`/`IIterable`/`IIterator` implementation. An element can't be added to two parents, and removing an element clears its `Parent`.
  - Pointer input is hit-tested through the tree, honouring `IsHitTestVisible` and panel backgrounds. Hover uses `WM_MOUSELEAVE` tracking; a press captures the button and the release decides the click.

**Deviations from the ladder:**

- Rendering is still GDI. The move to Direct2D/DirectWrite (anti-aliased geometry, transforms, `Opacity`, rounded corners) is now its own step before milestone 4, where templates first need it.
- The dependency-property store is still typed fields on each object, with change invalidation, not a `DependencyProperty`/`GetValue`/`SetValue` system. Markup (milestone 3) and styles (milestone 4) are where a generic store pays off, so it moves there.

**Tests** (fresh scratch prefix):

- `tests/xaml/xamllayout.c`, **34/34 checks pass**. It builds a Grid (`*`/`2*` columns, Auto/`*` rows) holding a StackPanel of TextBlocks and two Buttons, a blue Border with a child, and a green Border spanning both columns. It checks:
  - every arranged size: two-thirds column, Auto row equal to the StackPanel plus its margin, star row taking the rest, button height 32, StackPanel width
  - drawn colours: borders, button `#cccccc`, a hover border of 2 px `#999999`, a pressed background of `#999999`
  - Click on release only, with the right sender and source, and that the handler's text change is re-measured
  - no click when the release is outside the button, or on a disabled button
  - the Children collection: size, iteration, `Parent`, the second-parent refusal, removing and re-adding
- `xamlhello` (milestone 1) still passes 34/34.
- Halo Wars 2, 90 s windowed: normal render loop. Its binaries don't reference Windows.UI.Xaml at all; the only log differences from v1.0 were two lines of run-to-run noise in its startup (a thread-start race and the order of its first `PropertyValue` lookups).

**Still missing**, for milestone 3 and later:

- No keyboard input or focus (Tab, Space/Enter on a button).
- No `TextBox`, `ScrollViewer` or clipping: content larger than its slot is drawn past it.
- Grid `RowSpacing`/`ColumnSpacing` (`IGrid3`) and Grid/StackPanel border properties (`IGrid2`, `IStackPanel2`) aren't implemented; querying for those interfaces fails.
- `Border.CornerRadius` is drawn square.
- Hover and pressed colours assume the page background is the theme's (the real brushes are translucent).
- `PointerPressed`/`Tapped` and other `UIElement` routed events aren't raised; only `Button.Click` is.
- Still no XAML markup, so Visual Studio-built apps stop at `LoadComponent` as before.

## 9. Milestone 3: compiled markup, Page, Frame and resources (patches 0019-0022)

**What was built:**

- **XBF v2 reader and object writer** (`dlls/windows.ui.xaml/xbf.c`), following Microsoft's MIT-licensed reader (microsoft-ui-xaml, `dxaml/xcp/tools/XbfParser`):
  - It reads the metadata tables and node streams, and resolves built-in types and properties through the stable XBF index table (`xbf_stable.h`, generated from `StableXbfIndexes.g.h`).
  - It runs the object-writer instructions as a stack machine: create type, set value (constant, type-converted, `{StaticResource}`, `{ThemeResource}`), get a collection and add to it, keyed dictionary entries, `x:Name`, connection ids.
  - It reads all four kinds of custom runtime data (resource dictionaries v1–v4, styles v1–v3, visual-state groups v1–v5, `x:Load` elements v1–v3). Dictionary entries are built from their own node streams; the others are read past.
- **`Application.LoadComponent`** maps `ms-appx:///Folder/Page.xaml` to `Folder\Page.xbf` in the package folder. The markup's root element is the component itself. `x:Name` and event handlers go to the component's `IComponentConnector.Connect`, the way Visual Studio's generated code expects.
- **Custom types** (`local:MyControl`, pages) are created through the app's `IXamlMetadataProvider`, which the framework finds on the `Application` object.
- **New classes:**
  - `UserControl` and `Page`: composable, so app pages derive from them.
  - `Frame`: `Navigate(TypeName[, parameter])` creates the page through the metadata provider and shows it; `CurrentSourcePageType`; the navigation events are stored.
  - `ResourceDictionary`: an `IMap` with string keys, used by `Application.Resources` and `FrameworkElement.Resources`. Lookups walk the dictionaries in scope, then the application's, then a small built-in theme dictionary (`ApplicationPageBackgroundThemeBrush` and other common Windows 10 brushes, light and dark).
- **Property setting from markup** covers everything milestones 1 and 2 implemented: text, fonts, brushes from colours, names or `#AARRGGBB`, thickness, sizes, `GridLength` (`Auto`, `*`, `2*`), alignment, orientation, visibility, content, `Child`, Grid attached properties, `IsEnabled`, `ClickMode`, `RequestedTheme` and `Resources`.
- **0019 widl:** composable-only runtime classes were not written to the registry (only activatable or static ones), so `ResourceDictionary` couldn't be activated. Fixed.
- **0021 twinapi.appcore:** launch activation args now implement `IPrelaunchActivatedEventArgs` (false), `ILaunchActivatedEventArgs2`, `IApplicationViewActivatedEventArgs` and `IViewSwitcherProvider`.
  - The Visual Studio template's `OnLaunched` reads `e->PrelaunchActivated` first.
  - Before this, any unknown interface got a logging probe whose getter wrote an 8-byte NULL. That corrupted memory when the getter returned a `boolean` or `INT32`. Halo Wars 2 itself queried `IPrelaunchActivatedEventArgs` and hit this, and now gets the real interface.
- **`scripts/xbf.py`**: `dump` prints any XBF file's tables and node streams; `compile` turns a small XAML subset into XBF 2.1 laid out the way Microsoft's compiler does it. That is how the test app's markup is built, since there is no Windows toolchain here.

**Tests** (fresh scratch prefix, `tests/xaml/run.sh`):

- **`xamlmarkup.c`: 31/31 checks pass.** The app is structured like Visual Studio's generated C++/CX:
  - an `App` with `IXamlMetadataProvider` that loads `App.xaml` (light theme, two brushes in `Application.Resources`)
  - the Blank App `OnLaunched`: `PrelaunchActivated`, a new `Frame`, `Navigate(typeof(MainPage))`
  - a `MainPage` that aggregates `Page`, implements `IComponentConnector` and loads `MainPage.xaml`: a Grid with Auto and `*` rows, a StackPanel with a named TextBlock and a named Button with `Click="OnClick"`, and a Border whose background is `{StaticResource AccentBrush}` with a white TextBlock
  - The checks cover the theme, the dictionary contents, the frame asking the provider for `MainPage`, `Connect()` delivering both named elements, the text from markup, the layout, the resource brush and theme background in the drawn pixels, and clicking the button running `OnClick`.
- **Microsoft's compiler output:** 7 `.xbf` test files from microsoft-ui-xaml (MIT, fetched into the scratch folder and not committed; `XAML_MS_XBF_DIR=dir tests/xaml/run.sh` uses them) all load to the end with `S_OK`. Together they have 1 to 216 node streams, resource dictionaries with theme dictionaries, styles, visual states, conditional XAML, `x:Uid`, bindings and custom types. `scripts/xbf.py dump` of `CustomTypesPage.xbf` matches Microsoft's own dump of it node for node.
- `xamlhello` and `xamllayout` still pass 34/34.
- **Halo Wars 2**, 90 s windowed: normal render loop and swapchain. The only log difference from v1.0 is that the probe messages for `IPrelaunchActivatedEventArgs` are gone.

**Not working yet** (this is where milestone 4 starts):

- **Styles and templates:** `Style`, `ControlTemplate`, `DataTemplate`, deferred template properties and visual states are read but not applied. Built-in controls keep their hard-coded look.
- **Data binding:** `{Binding}` and other markup extensions are not created (logged). `x:Bind` is compiled into the app and should mostly work, but it is untested.
- **Dictionaries:** implicit (type-keyed) resources, `MergedDictionaries`, `ThemeDictionaries` and `ResourceDictionary.Source` are not supported.
- **Localization:** `x:Uid` localized strings need `resources.pri`, which is not supported.
- **Frame navigation** has no history (`GoBack`), no `OnNavigatedTo`/`NavigationEventArgs` and no navigation events.
- **Types:** properties and types the earlier milestones didn't implement are logged and skipped. Built-in types other than the twelve implemented ones come out as nothing, so their content is lost.
- **Text XAML** (`XamlReader.Load`, or `.xaml` files shipped uncompiled) is not supported, only `.xbf`.
- Not tested against an app built by Visual Studio: none can be built here. The compiled markup is checked against Microsoft's compiler output (above), but the C++/CX runtime (`vccorlib`) side is still unexercised.

## 10. Milestone 4, part 1: styles and resource dictionaries (patch 0023)

Milestone 4 is the largest rung (3 to 6 months in the ladder), so it lands in parts. Part 1 is styles and dictionaries, which work without control templates.

**What was built:**

- **`Style` from markup:**
  - `TargetType`, `BasedOn` and the Style_v1–v3 setter data the compiler writes (constant values, string values, `{StaticResource}`/`{ThemeResource}` values, which are single nodes setting `Setter.Value` in the style's node stream, and object values).
  - `FrameworkElement.Style` (get/put) applies the style, `BasedOn` first.
  - An element with no `Style` gets the **implicit style** for its type when its markup finishes initializing, if one is in scope. That means a type-keyed entry in a dictionary, or a keyless `Style` added to one.
- **Value precedence:** a property set directly on an element (in markup or by the app) is not overridden by a style. This is the "local beats style" part of WinUI's precedence rules, not a full dependency-property system.
- **ResourceDictionary:**
  - `ThemeDictionaries`: `{ThemeResource}` and lookups use the `Light`/`Dark` entry for the app's theme, else `Default`.
  - `MergedDictionaries`: searched last-first after the dictionary's own entries.
  - `Source`: `ms-appx:///Styles.xaml` loads that file's `.xbf` into the dictionary.
  - The `IMap` indexer finds theme and merged resources, as on Windows.
- **`scripts/xbf.py compile`** now handles `<Style>`/`<Setter>`, explicit and implicit dictionary entries, merged and theme dictionaries and `Source`, using the same layout as Microsoft's compiler. `dump` shows style setters.

**Tests:**

- **`xamlmarkup`: 45/45 checks pass.**
  - `App.xaml` merges `Styles.xaml` and defines Light/Dark theme dictionaries.
  - The implicit `TextBlock` style sets FontSize and Foreground, except where a TextBlock sets FontSize locally.
  - `ClickButton` uses `AccentButton` (BasedOn `BaseButton`): padding and font size come from the base, background from a `{StaticResource}` setter, and `Style.TargetType`/`BasedOn` read back.
  - A `{ThemeResource PanelBrush}` resolves to the Light theme's brush, checked in the drawn pixels.
- Milestones 1 and 2 still pass 34/34; Microsoft's 7 sample files still load.
- Halo Wars 2 isn't affected: only `windows.ui.xaml.dll` changed, and the game never loads it.

**Still to do in milestone 4:**

- **Control templates:** `ControlTemplate`/`DataTemplate` instantiation from their deferred node streams, `ContentPresenter`, `TemplateBinding` and `VisualStateManager`. Until then, `Template` setters are dropped and controls keep their built-in look.
- **The control set:** a built-in `generic.xaml` adapted from WinUI 3 (MIT), and CheckBox, RadioButton, ToggleSwitch, Slider and ProgressBar.
- **`{Binding}`** with `INotifyPropertyChanged`/`ICustomPropertyProvider`.
- **Direct2D/DirectWrite rendering**, which templates need for rounded corners, opacity and transforms.
- **Smaller gaps:**
  - `new Style()` from app code (the class isn't activatable yet), and `Style.Setters` as objects.
  - Setters given as whole objects (flag 0x40).
  - Styles keyed by anything but a string or type.
  - Re-evaluating `{ThemeResource}` when the theme changes at run time.

## 11. Milestone 4, part 2: control templates and visual states (patches 0024-0025)

**What was built:**

- **`ControlTemplate`:** its content is the deferred `FrameworkTemplate.Template` node stream. The template keeps its XBF file alive (the file is now reference-counted) and is instantiated for each control.
  - `Control.Template` can be set directly or from a style setter, including the `<Setter.Value><ControlTemplate>` object form.
  - A templated Button builds its template on first layout, as WinUI's `ApplyTemplate` does. It then measures, arranges, draws and hit-tests the template's root instead of its built-in look.
- **`TemplateBinding`:** evaluated once, when the template is applied. It covers Background, Foreground, BorderBrush, BorderThickness, Padding, Content, the content alignments, FontSize, Opacity, Visibility, size and margin. Later changes on the control don't flow into the template yet.
- **`ContentPresenter`:** shows a UIElement, or any other content as text, with background, border, padding, foreground and content alignment.
- **`VisualStateManager`:**
  - Groups, states and storyboards are built from the visual-state custom data's node stream as internal objects.
  - `GoToState` applies `VisualState.Setters` (`Target="Name.Property"`) and storyboard animations: `DoubleAnimation`/`ColorAnimation` `To`, key-frame animations' last frame, and `(Border.Background).(SolidColorBrush.Color)`-style paths, which get a new brush.
  - Animations jump to their final value. There are no timelines, easing or transitions yet.
  - Leaving a state restores the values it changed. The template's `x:Name`s form their own name scope.
  - A templated Button goes through `Normal`, `PointerOver`, `Pressed` and `Disabled`.
- **Value resources** (`<x:Double>`, `<Thickness>`, booleans, strings) are kept as boxed values; theme dictionaries are full of them. `Brush.Opacity` can be set from markup.
- `scripts/xbf.py compile` handles `ControlTemplate`, `TemplateBinding`, `ContentPresenter`, `VisualStateManager.VisualStateGroups` with states, setters, storyboards and key frames, and style setters with object values.

**Tests:**

- **`xamlmarkup`: 54/54 checks pass.** `Styles.xaml` defines a `TemplatedButton` style whose template has a Grid with a `TemplateBinding` background, a Border named `Frame` around a `ContentPresenter`, and `CommonStates`:
  - `PointerOver` uses a Setter.
  - `Pressed` uses `ObjectAnimationUsingKeyFrames` on the Grid's background and a `ColorAnimation` on the Border's brush colour.
  - The test checks the drawn template, the presenter's text and padding, each state's colours as the mouse hovers, presses and releases, and that leaving Pressed and PointerOver restores the earlier values.
- Milestones 1 and 2 still pass 34/34. Microsoft's 7 sample files still load, and value resources in their theme dictionaries are now kept instead of dropped.

**Still to do in milestone 4:**

- Live `TemplateBinding` (changes after the template is applied), and templates for Page, UserControl, Frame and `ContentControl` in general (only Button is templated so far).
- Animation timing and visual transitions.
- `DataTemplate`/`ItemsPanelTemplate`.
- A default `generic.xaml` adapted from WinUI 3 (MIT), so built-in controls get their real templates; and CheckBox, RadioButton, ToggleSwitch, Slider, ProgressBar.
- `{Binding}`.
- Direct2D rendering: rounded corners, opacity and gradients, which real templates use heavily.

## 12. Milestone 4, part 3: toggle controls, live TemplateBinding, WinUI resource keys (patches 0026-0027)

**What was built:**

- **`ToggleButton`, `CheckBox` and `RadioButton`** (patch 0026 is their IDL, generated by `scripts/winmd2idl.py` with `--have` on a tree with 0001-0025: `Controls.Primitives.ToggleButton Controls.CheckBox Controls.RadioButton`; the `IReference<boolean>` declare block was dropped because `windows.foundation.idl` already has it).
  - They share Button's code (`button.c`), which now has a kind per class and composable factories for all four.
  - `IsChecked` (`IReference<boolean>`, null meaning indeterminate), `IsThreeState`, and `Checked`/`Unchecked`/`Indeterminate` events, raised before `Click` as `ToggleButton.OnToggle` does.
  - A click cycles unchecked, checked, then indeterminate when three-state. A RadioButton click only checks.
  - Checking a RadioButton unchecks the others in its group: the same `GroupName` anywhere in the tree, or without one the same parent.
  - Markup can set `IsChecked`, `IsThreeState` and `GroupName`, and `Checked`/`Unchecked`/`Indeterminate` handlers are connected like `Click`.
- **Templates for the toggle controls.** Each visual state group goes to the first state it has from a priority list (`template_go_to_states`), which covers the three schemes WinUI's templates use:
  - ToggleButton's single `CommonStates` group (`Normal`, `PointerOver`, `Checked`, `CheckedPointerOver`, ...);
  - CheckBox's `CombinedStates` (`UncheckedNormal`, `CheckedPointerOver`, ...);
  - RadioButton's (and older CheckBox templates') separate `CommonStates` and `CheckStates`.
- **Built-in looks without a template.** CheckBox and RadioButton draw a 20 px box or circle in a 32 px row with the content to its right, using the default styles' padding and alignment.
  - They and Button/ToggleButton take colours from the WinUI resource keys an app may override, looked up in the element's and its ancestors' `Resources` and then the application's: `ButtonBackground`/`Foreground`/`BorderBrush` plus state (`PointerOver`, `Pressed`, `Disabled`), `ToggleButton...Checked...`, `CheckBoxCheckBackgroundFill`/`Stroke`/`CheckGlyphForeground` + check state + state, and `RadioButtonOuterEllipse...`/`CheckGlyphFill`.
  - In the Normal state the control's own brushes win, as the default templates' `TemplateBinding`s do. Without app overrides the Windows 10 colours are used.
  - This is not the real default template: an app that relies on parts of WinUI's `generic.xaml` (named parts, its exact layout) still sees the approximation.
- **Live `TemplateBinding`.** A template instance keeps its bindings. When the control's Background, Foreground, BorderBrush, BorderThickness, Padding, content alignments, FontSize, IsEnabled or Content changes, the bound template parts are updated.
  - If a visual state currently overrides the part's property, the new value instead becomes what leaving the state restores, as animated values outrank local ones in WinUI.
  - The content element moves into the template's ContentPresenter.
- **`Control.Template`, `ApplyTemplate`** from code work for these controls.

**Tests:**

- **`xamlmarkup`: 75/75 checks pass.** The page has two CheckBoxes (one three-state, checked from markup), two RadioButtons, a templated ToggleButton and a plain Button. `Styles.xaml` overrides `CheckBoxCheckBackgroundFillChecked` and `ButtonBackgroundPointerOver`. The test checks:
  - the `IsChecked` values from markup, `Checked`/`Unchecked` handler counts, and the app's fill colour on checked boxes;
  - the three-state cycle, and radio grouping by click and by `IsChecked` from code;
  - the ToggleButton template through `PointerOver`, `CheckedPointerOver` and `Checked`;
  - a `Background` set while `Checked` staying hidden until the state is left;
  - a `Background` set on the templated Button reaching its template immediately;
  - the plain Button's hover colour coming from the app's `ButtonBackgroundPointerOver`.
- Milestones 1 and 2 still pass 34/34. Microsoft's 7 sample files still load.

**Still to do in milestone 4:**

- WinUI 3's MIT-licensed `generic.xaml` as the real default templates. It needs a way to ship compiled markup in the DLL (`xbf.py` output as a resource) and more of what those templates use: `CornerRadius`, `ContentPresenter` text properties, `x:Load`, `{ThemeResource}` in setters at state time.
- `ToggleSwitch`, `Slider`, `ProgressBar`.
- Templates for Page, UserControl, Frame and `ContentControl` in general.
- Animation timing and visual transitions.
- `DataTemplate`/`ItemsPanelTemplate`.
- `{Binding}`.
- Direct2D rendering.
