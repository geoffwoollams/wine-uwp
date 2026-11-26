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

## 13. Milestone 4, part 4: XAML text, XamlReader and a built-in generic.xaml (patches 0028-0029)

**What was built:**

- **A XAML text reader.** A small XML parser builds a tree (namespaces, attributes, entities, comments, CDATA), which drives the same object writer as XBF.
  - **Attributes and elements:** attributes set properties, with `Owner.Property` for attached ones. Property elements set or fill properties. Other children go to the type's content property (Panel children, Border child, `Content`, `Text`, dictionary items, style setters, visual states, storyboard children, key frames).
  - **Markup extensions:** `{StaticResource}`, `{ThemeResource}`, `{TemplateBinding}` and `{x:Null}`. `x:Name`, `x:Key` and implicit style keys work.
  - **Value elements:** `x:String`, `x:Double`, `x:Int32`, `x:Boolean`, `Thickness` and `Color`.
  - **Templates:** a text `ControlTemplate` keeps its part of the tree and builds it for each control.
  - **Not supported:** event handler attributes are ignored, as `XamlReader` doesn't allow them; `{Binding}` is not supported.
- **`XamlReader.Load`** (patch 0028 is its IDL, generated with `winmd2idl.py ... Windows.UI.Xaml.Markup.XamlReader`).
- **`generic.xaml`**, an `RCDATA` resource of `windows.ui.xaml.dll`, written for wine-uwp rather than copied from Microsoft. It follows the structure apps depend on: WinUI's resource keys, visual state names and template part names, with the Windows 10 values.
  - Light and Dark `ThemeDictionaries` with the `SystemControl*` brushes and the Button and ToggleButton keys.
  - `BaseTextBlockStyle`, `HeaderTextBlockStyle`, `SubheaderTextBlockStyle`, `TitleTextBlockStyle`, `SubtitleTextBlockStyle`, `BodyTextBlockStyle` and `CaptionTextBlockStyle`.
  - Default styles for Button and ToggleButton, with a `ContentPresenter` template whose states animate `{ThemeResource ButtonBackgroundPointerOver}` and the like. CheckBox and RadioButton get default property values; they still draw themselves.
  - Resource lookups go: the markup's scope, the templated control's ancestors, the application, then `generic.xaml`, then the old built-in table.
- **Default styles** (`DefaultStyleKey`) apply on first layout at a new, lowest precedence: local values, then style values (now marked), then the default style.
  - `generic.xaml`'s setter resources are looked up when applied, from the element up, so an app's `ButtonBackground` wins over the built-in one.
  - In app markup, `{StaticResource}` in setters resolves where it is written, as on Windows.
- **Translucent brushes:** fills and borders are alpha-blended (`GdiAlphaBlend`). Translucent text is blended against the pixel under it, since GDI text has no alpha.
- **`FontWeight`** from markup (names or numbers) for TextBlock and controls.

**Tests:**

- **`xamlmarkup`: 86/86 checks pass** (79 without Microsoft's sample files). New checks:
  - `XamlReader.Load` refuses malformed XML.
  - It loads a StackPanel with resources, a Style and a text ControlTemplate, and the children and decoded entities are right.
  - A TextBlock with `TitleTextBlockStyle` (BasedOn `BaseTextBlockStyle`) is 24 px SemiLight.
  - The loaded content shown in the page draws the template with the style's resource, and its PointerOver state.
  - A plain Button has generic.xaml's template and `{ThemeResource ButtonBackground}` (#33000000).
- **`xamllayout`: 34/34 checks pass.** Its hover check now expects 0x7a7a7a, the default template's `#66000000` border drawn over the `#33000000` background (Windows 10's Button paints its background under the border), where the earlier hand-drawn approximation had 0x999999.
- **`xamlhello`:** 34/34.

**Still to do in milestone 4:**

- CheckBox and RadioButton templates (they need `Rectangle`/`Ellipse` shapes and a glyph font or `Path`), and ToggleSwitch, Slider, ProgressBar.
- `CornerRadius`.
- `{ThemeResource}` updates when the theme changes at run time.
- `LoadComponent` of loose `.xaml` files.
- Animation timing.
- `DataTemplate`.
- `{Binding}`.
- Direct2D rendering.

## 14. Milestone 4, part 5: shapes, CornerRadius, CheckBox/RadioButton templates, theme changes, loose XAML (patches 0030-0031)

**What was built:**

- **Shapes** (patch 0030 is their IDL, generated with `winmd2idl.py ... Windows.UI.Xaml.Shapes.Shape Shapes.Rectangle Shapes.Ellipse Shapes.Path`).
  - `Rectangle` (with `RadiusX`/`RadiusY`) and `Ellipse` fill their layout slot and draw their stroke inside it.
  - `Path` takes path markup from markup: `M`, `L`, `H`, `V` and `Z`, absolute and relative, with curves reduced to their end points. It draws at its own coordinates. `Geometry` objects from code are not supported.
  - Strokes and fills are GDI regions, so translucent brushes blend.
- **`CornerRadius`** on Border (`IBorder`), ContentPresenter and controls, from markup and through `TemplateBinding`. Each corner has its own radius, drawn as polygon regions with the stroke inside the edge. Without corners, the old rectangle code draws pixel for pixel as before.
- **CheckBox and RadioButton default templates** in `generic.xaml`, built from shapes:
  - **CheckBox:** a `CombinedStates` group (`UncheckedNormal` ... `IndeterminateDisabled`); a 20 px `NormalRectangle`; a `Path` check mark (Windows uses a Segoe MDL2 Assets glyph); an indeterminate square.
  - **RadioButton:** `CommonStates` and `CheckStates`, with `OuterEllipse`, `CheckOuterEllipse` and `CheckGlyph`.
  - **Both:** WinUI's resource keys (`CheckBoxCheckBackgroundFillChecked`, `RadioButtonOuterEllipseCheckedStroke`, ...), with Windows 10 values for both themes.
  - The built-in drawing in `button.c` is now only a fallback.
- **Theme changes while running.**
  - **What is remembered:** `{ThemeResource}` values set on elements, from XBF or text, and the theme-dependent setters of explicit, implicit and default styles. The implicit style is now kept on the element.
  - **`FrameworkElement.RequestedTheme`** (`IFrameworkElement2`, and markup) marks the subtree. On its next layout the subtree looks these values up again under the element's theme. Theme dictionaries consult an override while this happens. Templates are rebuilt, and drawing (built-in looks, default text colour) follows the subtree's theme.
  - **The system theme:** the app theme now starts from the system's (`AppsUseLightTheme`) unless App.xaml or code sets `RequestedTheme`. `WM_SETTINGCHANGE` "ImmersiveColorSet" refreshes the tree when it changes.
- **Loose XAML.** `LoadComponent` reads `Page.xaml` (UTF-8 or UTF-16) when there is no `Page.xbf`.
  - `x:ConnectionId`, which Visual Studio writes into processed markup when XBF generation is off, reaches the component's `Connect`, so `x:Name` fields and event handlers are hooked up.
  - `FrameworkElement.FindName` searches the element's tree.
- **Quieter logs.** The markup writer probes objects for many interfaces. Elements, brushes and dictionaries now FIXME a missing interface once per class, then TRACE. The test log went from about 28,000 lines to about 2,000.

**Tests:**

- **`xamlmarkup`: 105/105 checks pass.** New checks:
  - CheckBox and RadioButton get the default templates. The checked box is the template's 20 px Rectangle in the app's `CheckBoxCheckBackgroundFillChecked`, with the Path check mark inside.
  - A `CornerRadius="10"` Border has rounded corners: the corner pixel is outside, the edge and fill inside.
  - `RequestedTheme=Dark` on the page's root Grid switches App.xaml's `PanelBrush` and generic.xaml's page background and `ButtonBackground` to their dark values. `Default` brings the light ones back.
  - A system theme change doesn't affect an app with an explicit `RequestedTheme`.
  - A loose `LoosePage.xaml` loads through `LoadComponent`, its `x:ConnectionId` reaches `Connect`, its `CornerRadius` is read, and `FindName` finds its TextBlock.
- Milestones 1 and 2 still pass 34/34.

**Still to do in milestone 4:**

- ToggleSwitch, Slider, ProgressBar and their templates.
- Path curves and `Geometry` objects.
- Opacity rendering.
- Anti-aliased shapes (GDI draws them aliased).
- Animation timing.
- `DataTemplate`.
- `{Binding}`.
- Direct2D rendering.

## 15. Milestone 4, part 6: controls, DataTemplate, {Binding}, animation timing, Opacity, geometries (patches 0032-0033)

Patch 0032 is the IDL, generated with `winmd2idl.py` for:
- `Controls.ToggleSwitch`, `Slider`, `ProgressBar`, `Primitives.RangeBase`, `ItemsControl`, `ItemsPanelTemplate` and `ItemCollection`;
- `Media.Animation.Storyboard` and `Timeline`;
- `Data.INotifyPropertyChanged`, `ICustomPropertyProvider`, `ICustomProperty`, `PropertyChangedEventArgs` and `IValueConverter`;
- `Interop.IBindableIterable`, `IBindableIterator` and `IBindableVector`;
- `Media.EllipseGeometry`, `LineGeometry` and `RectangleGeometry`;
- `DataTemplate` and `FrameworkTemplate`.

`GetCurrentTime` (a winbase macro) is now in the generator's list of names to `#undef`.

**What was built:**

- **`ToggleSwitch`, `Slider`, `ProgressBar`** (`range.c`) share Button's `Control` code. The control struct moved to `control.h`.
  - They draw the Windows 10 look themselves, taking WinUI's resource keys from the app (`ToggleSwitchFillOn`, `SliderTrackValueFill`, `ProgressBarForeground`, ...).
  - **Slider:** horizontal or vertical. Pressing and dragging set `Value`, snapped to `StepFrequency`, and `ValueChanged` is raised.
  - **ProgressBar:** a value bar, an indeterminate moving segment, `ShowError` and `ShowPaused`.
  - **ToggleSwitch:** `IsOn`, `Header`, `OnContent`/`OffContent` and `Toggled`.
  - App templates for these aren't used.
- **`DataTemplate` and `ItemsPanelTemplate`** are templates like ControlTemplate, from XBF or text, with `LoadContent`.
  - `ContentControl.ContentTemplate` and `ContentPresenter.ContentTemplate` show non-element content through the template, with the content as its DataContext. generic.xaml's presenters bind `ContentTemplate`.
- **`ItemsControl`** (`items.c`):
  - **Sources:** `Items`, which markup content also fills, or `ItemsSource` as an `IIterable<Object>` or `IBindableIterable`. An observable `IObservableVector<Object>` regenerates on change.
  - **Display:** `ItemTemplate` with each item as its DataContext, otherwise UIElements as they are and anything else as text.
  - **Panel:** `ItemsPanel`, a StackPanel by default.
  - **Not supported:** containers, ItemsPresenter, grouping and `DisplayMemberPath`.
- **`{Binding}`** (`binding.c`), from XBF (the Binding object the compiler writes) or text (`{Binding Path, Mode=..., ElementName=..., Converter={StaticResource ...}}`):
  - **Sources:** `Source`, `ElementName`, or the inherited `FrameworkElement.DataContext`.
  - **Paths:** dotted paths read through `ICustomPropertyProvider`, the app's `IXamlMetadataProvider` (`IXamlMember`), or our own elements' properties.
  - **Updates:** `INotifyPropertyChanged` on the last object, and our controls' own changes (`Value`, `IsOn`, `IsChecked`), update the target. `TwoWay` writes target changes back through `ICustomProperty`/`IXamlMember.SetValue`.
  - **Also:** `OneTime`, `Converter`/`ConverterParameter`, `FallbackValue`, `TargetNullValue`, and conversion to text for text properties. `PropertyChangedEventArgs` can be created by apps.
  - **Not supported:** `RelativeSource`, `UpdateSourceTrigger`, indexers and `SetBinding` from code.
- **Animation timing** (`animation.c`): storyboard animations become tracks sampled by a 60 Hz thread timer.
  - **Animation kinds:** `DoubleAnimation`/`ColorAnimation` (From/To/By, `Duration` defaulting to 1 s), key-frame animations (discrete frames hold; linear, easing and spline frames are interpolated linearly) and `ObjectAnimationUsingKeyFrames`.
  - **Timing:** `BeginTime`, `AutoReverse`, and `RepeatBehavior` as `Forever` or a count.
  - **Visual states:** animations play over time and are stopped when the group leaves the state.
  - **From code:** a Storyboard from markup is an `IStoryboard`/`ITimeline`. `Begin` resolves `TargetName` from the markup's root, and `Stop`, `SkipToFill`, `GetCurrentState` and `Completed` work.
  - **Not supported:** easing functions, SpeedRatio, Seek, Pause and visual transitions.
- **`Opacity`** is drawn: an element below 1 is rendered over a copy of what is under it and blended back, which also works nested.
- **Path data:** `C`, `S`, `Q`, `T` and `A` are flattened into lines. `Path.Data` also takes `RectangleGeometry`, `EllipseGeometry` and `LineGeometry`, from markup or code. `PathGeometry` and transforms aren't supported.
- **`tests/xaml/run.sh`** removes the throwaway prefix it creates. Each run had left about 800 MB in `/tmp`, which filled a RAM-backed `/tmp`.

**Tests:**

- **`xamlmarkup`: 128/128 checks pass.** A page loaded with XamlReader and shown over the main page checks:
  - Opacity 0.5 blending;
  - a curved Path;
  - an EllipseGeometry's filled area;
  - the ProgressBar's half fill;
  - ToggleSwitch `IsOn`/`Toggled` and its accent track;
  - Slider's `Value`;
  - `Storyboard.Begin` from 1 through about 0.6 to 0 with `Completed`;
  - a visual state's `ColorAnimation` part way at 150 ms and done at 400 ms, then restored;
  - `{Binding Name}` and its `PropertyChanged` update;
  - an ItemsControl drawing three items through a DataTemplate's `{Binding Color}`;
  - `Button.ContentTemplate` with `{Binding}`;
  - a TwoWay Slider reading 40 from the data object and writing 70 back;
  - an ElementName binding following it.
- Milestones 1 and 2 still pass 34/34. Microsoft's sample files load, and their Bindings are now Binding objects rather than unsupported types.
- Slider dragging and a ToggleSwitch click aren't driven by the test; `IsOn`/`Value` are set from code.

**Still to do (next round):**

- Direct2D rendering: anti-aliasing, transforms, `RenderTransform`, and clipping.
- ListView/GridView.
- `TextBox`.
- Scrolling.
- Keyboard focus.
- Visual transitions and easing.

## 16. The Direct2D round: Direct2D and DirectWrite rendering, transforms, clips, gradients (patches 0034-0035)

Patch 0034 is the IDL, generated with `winmd2idl.py` for `Media.Transform`, `GeneralTransform`, `TranslateTransform`,
`ScaleTransform`, `RotateTransform`, `CompositeTransform`, `MatrixTransform`, `LinearGradientBrush`, `GradientBrush`,
`GradientStop` and `GradientStopCollection`.

**What was built:**

- **`render.c`:** each frame is drawn by one Direct2D DC render target bound to the window's back buffer, in view pixels (its DPI is 96 x the view scale). Text is laid out and drawn with DirectWrite, and measured with it too, so layout matches drawing. If Direct2D can't start, element.c draws with GDI as before.
- **Shapes:** fills, borders, rounded corners, ellipses and paths are anti-aliased.
  - Corner and ellipse arcs are cubic Béziers, because Wine's `ID2D1GeometrySink::AddArc` is a stub.
  - Strokes inside the edge are two-figure alternate-fill geometries, because `CombineWithGeometry` is a stub.
- **Opacity** draws the subtree into a compatible bitmap render target and blends it in with `DrawBitmap`, because Wine's `PushLayer` is a stub. Translucent text keeps its alpha: the inherited foreground carries an alpha value.
- **`media.c`:**
  - **Transforms:** TranslateTransform, ScaleTransform, RotateTransform, CompositeTransform (scale, skew, rotate about the centre, then translate) and MatrixTransform, each with `IGeneralTransform` (`Inverse`, `TransformPoint`, `TransformBounds`).
  - **Gradients:** `LinearGradientBrush` with `GradientStops`, `StartPoint`/`EndPoint`, `MappingMode` and `SpreadMethod`, plus `GradientStop`.
  - **Not supported:** TransformGroup, brush transforms and ColorInterpolationMode.
- **Elements:**
  - `UIElement.RenderTransform` about `RenderTransformOrigin` is applied when drawing and inverted for hit testing.
  - `UIElement.Clip` (a RectangleGeometry) is an axis-aligned clip in the element's space. Under a rotation it clips to the bounding box.
  - Without Direct2D, transforms and clips are ignored.
- **Markup and animations:** the new types and properties work from XAML. Storyboards can target `(UIElement.RenderTransform).(RotateTransform.Angle)`-style paths, which resolve to the transform object.
- **Speed:** frames take about 16-25 ms at 1280x720. The first frame of a process takes about half a second (Direct3D start-up).
- **Test harness:** the markup test's `pump()` could run past its time when frames outlast the animation timer; it now checks the clock between messages.

**Tests:**

- **`xamlmarkup`: 138/138 checks pass**, all under Direct2D.
  - **New checks:** a LinearGradientBrush red at its start, blue at its end and mixed between; a RotateTransform of 90 degrees about the top left turning a bar downwards; a Clip drawing only the left half of a Border; a TranslateTransform moving a square.
  - **Existing checks:** the pixel checks of earlier rounds (corners, opacity, ellipse area, check marks, theme colours) still pass with anti-aliasing.
- Milestones 1 and 2 still pass 34/34.

**Still to do:**

- ListView/GridView.
- TextBox.
- Scrolling (ScrollViewer).
- Keyboard focus.
- Visual transitions and easing.
- Image and ImageBrush (WIC).
- TransformGroup and rotated clips.
- Faster frames: drawing straight to the window instead of through the DC render target's copies.

## 17. Milestone 5, part 1: ScrollViewer, keyboard focus, Tab navigation and key events (patches 0043-0044)

Milestone 5 (everyday app controls) lands in parts like milestone 4. Part 1 is the two pieces every other control in it
leans on: scrolling, and the keyboard.

Patch 0043 is the IDL, generated with `winmd2idl.py` for `Controls.ScrollViewer` (with `IScrollViewer2` to `4` and its
statics), `ScrollViewerViewChangedEventArgs` and `Input.FocusManager`. Declarations already in the file are left out of
the new `declare` block, because widl rejects them twice, and `AsyncOperationCompletedHandler<FocusMovementResult>`
is added, which the generator missed.

**What was built:**

- **`ScrollViewer`** (`scroll.c`) is a ContentControl kind of `struct button`, like the range controls.
  - **Layout:** the content is measured without a limit along each axis that scrolls, arranged at minus the offset and drawn clipped to the viewport. Windows' defaults apply: vertical scrolling with a visible bar, horizontal disabled, where the content gets the viewport's width.
  - **API:** `HorizontalOffset`/`VerticalOffset`, `Extent*`, `Viewport*`, `Scrollable*`, `Computed*ScrollBarVisibility`, `ChangeView` (both overloads), `ScrollToHorizontalOffset`/`ScrollToVerticalOffset` and `ViewChanged`. The scroll modes, bar visibilities and the other `IScrollViewer` settings are kept. Changes happen at once, so there is no animation and `IsIntermediate` is always false.
  - **Input:** the mouse wheel scrolls 48 px a notch, and Shift or a horizontal wheel scrolls sideways. The arrow keys scroll 16 px, Page Up/Down a viewport, Home/End to the ends.
  - **Scroll bars:** Windows 10's look, a thin indicator that widens to a 12 px bar while the pointer is over the ScrollViewer. The thumb can be dragged, and a press on the track pages.
  - **Markup:** `ScrollViewer.*` properties set on a ScrollViewer work from XAML. Attached to other elements (ListView templates do this) they are accepted and ignored.
  - **Hit testing:** a new element operation, `child_clip`, keeps content scrolled out of view from taking clicks.
  - **Not supported:** zooming (`ZoomFactor` stays 1), snap points, headers (kept, not drawn), inertia and touch panning.
- **Keyboard focus** (`focus.c`): one element has the focus and is referenced while it does.
  - **Taking it:** `Control.Focus`, `IsTabStop`, `TabIndex` and `FocusState` work, and a pointer press focuses the nearest control that takes it.
  - **Tab:** Tab and Shift+Tab go through the focusable elements in `TabIndex` order, then tree order, wrapping round.
  - **Focus visual:** a keyboard-focused control gets Windows 10's two-colour outline.
  - **Events:** UIElement's `GotFocus`/`LostFocus` bubble from the element.
- **Key events:** `KeyDown`/`KeyUp` with `KeyRoutedEventArgs` (`Key`, `KeyStatus`, `Handled`, `OriginalSource`) bubble from the focused element, or from the window's content when nothing is focused.
  - Keys no handler marked handled get the controls' defaults: Space presses a button and clicks it on release, Enter clicks a Button, Space toggles a ToggleSwitch, the arrow keys, Page Up/Down, Home and End move a Slider, and the ScrollViewer keys above.
  - CoreWindow still raises its own key events.
- **`FocusManager`:** `GetFocusedElement` (both overloads), `TryMoveFocus`, `FindNextFocusableElement`, `FindNextElement`, `FindFirstFocusableElement`/`FindLastFocusableElement`, and the static `GotFocus`/`LostFocus` events.
  - Directional (XY) navigation follows the Tab order.
  - `GettingFocus`/`LosingFocus` and the async methods aren't implemented.

**Tests:**

- **`xamlmarkup`: 161/161 checks pass.** 30 are new. A ScrollViewer over three 80 px items checks:
  - its extent (240), viewport (100) and scrollable height (140), and the disabled horizontal axis;
  - the content clipped below the viewport;
  - `ChangeView` to 100, the moved pixels and `ViewChanged`;
  - the wheel up a notch and three notches down to the end;
  - `ScrollToVerticalOffset`.
- **Focus checks:**
  - `Focus(Programmatic)` with `FocusState`, `GotFocus` and `FocusManager.GetFocusedElement`;
  - Tab to the next button, focused by the keyboard and drawn with the focus visual;
  - KeyDown bubbling to the buttons' panel;
  - Space clicking on release, Enter clicking;
  - Tab skipping `IsTabStop=False`.
- Milestones 1 and 2 still pass 34/34.

## 18. Milestone 5, part 2: TextBox and PasswordBox (patches 0045-0046)

Patch 0045 is the IDL, generated with `winmd2idl.py` for `Controls.TextBox`, `PasswordBox` and `TextChangedEventArgs`
(all their interface versions, `InputScope` and the text control event args). It needs
`AsyncOperationCompletedHandler<IVectorView<HSTRING>>` declared, which the generator missed. Patch 0046 is the code
(`textbox.c`).

**What was built:**

- **`TextBox` and `PasswordBox`** are Control kinds of `struct button`, so `IControl` (fonts, brushes, focus) is shared with the other controls.
  - **Interfaces:** `ITextBox` and `ITextBox2`; `IPasswordBox` and `IPasswordBox2`. TextBox also has `ITextBoxFactory`, so apps can subclass it.
  - **Look:** Windows 10's, with a 2 px border that turns the accent colour while focused, `Header` above as text, and `PlaceholderText` in grey while the box is empty. A PasswordBox shows `PasswordChar` (● by default) for each character.
- **Layout and drawing:**
  - Text is laid out with DirectWrite, which also places the caret and maps a click to a character. `render.c` gains `dwrite_caret` and `dwrite_hit`.
  - A single-line box scrolls sideways to keep the caret in view. With `AcceptsReturn` it has lines and grows with them, and wraps if `TextWrapping` says so.
  - **Wine bug worked round:** Wine's `IDWriteTextLayout::HitTestTextPosition` faults on a layout without characters, so an empty text is measured as a space.
- **Editing:**
  - **Typing:** characters (`WM_CHAR`, through a new element operation and `focus_char`) replace the selection.
  - **Keys:** Backspace and Delete, the arrow keys with Shift to select and Ctrl to move by word, Home and End, Up and Down between lines, and Enter for a new line with `AcceptsReturn`.
  - **Clipboard:** Ctrl+A, and Ctrl+C / X / V for TextBox only.
  - **Mouse:** a press places the caret, a drag selects, and Shift+press extends the selection.
  - **Limits:** `MaxLength` and `IsReadOnly` hold.
- **API:**
  - **Text:** `Text`, `SelectedText`, `SelectionStart`/`SelectionLength`, `Select`, `SelectAll` and `GetRectFromCharacterIndex`; `Password` and `PasswordChar`.
  - **Events:** `TextChanged` (with `TextChangedEventArgs`), `SelectionChanged` and `PasswordChanged` are raised at once, not queued as on Windows. Bindings to `Text` and `Password` hear of changes.
- **Markup:** `Text`, `Password`, `PlaceholderText`, `Header` (as text), `PasswordChar`, `AcceptsReturn`, `IsReadOnly`, `MaxLength` and `TextWrapping`.
- **Not supported:**
  - IME composition, undo, spell checking, the touch keyboard, the password reveal button and the context menu;
  - `HeaderTemplate`, the selection highlight brush, and the interface versions above 2;
  - the selection is drawn as a translucent accent band over the text, where Windows draws white text on the accent colour.

**Tests:**

- **`xamlmarkup`: 185/185 checks pass**, 24 of them new:
  - the placeholder from markup and drawn in grey, and the 32 px height;
  - typing with `TextChanged`, and the caret after the text;
  - Left, replacing a selection, Backspace, Delete, `SelectAll`;
  - `MaxLength` and `IsReadOnly`;
  - Ctrl+C and Ctrl+V twice through the clipboard;
  - clicks at the text's start and end placing the caret;
  - typing a password with `PasswordChanged`;
  - Enter adding a line to an `AcceptsReturn` box, which then grows.
- Milestones 1 and 2 still pass 34/34.

## 19. Milestone 5, part 3: Image, BitmapImage and ImageBrush (patches 0047-0048)

Patch 0047 is the IDL. `winmd2idl.py` generated `Controls.Image`, `Media.ImageBrush`, `TileBrush`, `ImageSource`, and
`Media.Imaging.BitmapSource` with the rest of BitmapImage's interfaces (`IBitmapImage2`, `3` and `IBitmapImageFactory`).
`IBitmapImage` itself was already in the file, from milestone 1's generation.
- That earlier `runtimeclass BitmapImage` had no `activatable` attributes, so widl didn't register the class. They are added, with a forward declaration of the factory interface.
- `windows.storage.streams.idl` is now imported.

Patch 0048 is the code (`image.c`).

**What was built:**

- **`BitmapImage`** decodes with WIC into 32-bit premultiplied BGRA pixels.
  - **Where from:** `ms-appx:///` and `ms-appx://package/` resolve in the app's package (or, without one, the executable's folder), and plain paths work too. Markup paths without a scheme are relative to the package.
  - **API:** `PixelWidth`/`PixelHeight`, `DecodePixelWidth`/`DecodePixelHeight` (one of them keeps the aspect ratio), `UriSource`, and the `IBitmapImageFactory` constructor with a URI.
  - **Events:** decoding happens at once, but `ImageOpened`/`ImageFailed` (with `ExceptionRoutedEventArgs`) follow from a UI-thread timer. Callers add handlers after setting the source, as with Windows' asynchronous loading.
  - **Not supported:** `SetSource` with a stream (Wine's `CreateStreamOverRandomAccessStream` is a stub), web URIs (no network), `ms-appdata`, and animated images (the first frame is shown).
- **`Image`** draws its source by `Stretch`, Uniform by default, centred in its slot and clipped when it is bigger.
  - Its natural size is the bitmap's pixel size. An open axis takes the picture's proportions.
  - `ImageOpened`/`ImageFailed` are raised for the Image as well as the bitmap.
  - `NineGrid` is kept but not used.
- **`ImageBrush`** (`IImageBrush`, `ITileBrush`, `IBrush`) works anywhere a brush is filled: Background, Fill, and so on.
  - Under Direct2D it becomes a bitmap brush placed by `Stretch` (Fill by default), `AlignmentX` and `AlignmentY`, with the brush's `Opacity`.
  - Brush transforms are kept but not applied.
- **`render.c`:** `draw_image` (Direct2D `DrawBitmap`, or `GdiAlphaBlend` from a DIB without Direct2D), and bitmap brushes in `d2d_brush`.
- **Markup:** `Image.Source` and `ImageBrush.ImageSource` as URI strings or objects, `BitmapImage.UriSource`/`DecodePixelWidth`/`DecodePixelHeight`, `Stretch`, `AlignmentX`, `AlignmentY`.

**Tests:**

- `tests/xaml/build.sh` makes a 40×20 PNG, red on the left and blue on the right, with Python's standard library rather than committing a binary. `run.sh` copies it next to the test.
- **`xamlmarkup`: 198/198 checks pass**, 13 of them new:
  - `ImageOpened` once the picture is decoded, and `ImageFailed` for a missing file;
  - the source's 40×20 pixel size, and `Stretch=None` taking that size with red and blue halves;
  - `Stretch=Fill` scaling it to 80×40;
  - an ImageBrush painting a Border;
  - `DecodePixelWidth=20` decoding it at 20×10 from code.
- Milestones 1 and 2 still pass 34/34.

## 20. Milestone 5, part 4: ListView, GridView and their items (patches 0049-0050)

Patch 0049 is the IDL. `winmd2idl.py` generated `Controls.ListView`, `GridView`, `ListViewBase`, `ListViewItem`,
`GridViewItem`, `ItemsStackPanel`, `ItemsWrapGrid`, `Primitives.Selector` and `SelectorItem`, with their event
arguments and delegates.
- The generator missed `AsyncOperationCompletedHandler<LoadMoreItemsResult>` again; it is declared by hand.

Patch 0050 is the code: `items.c` (ListView and GridView), the new `listview.c` (the items) and `panel.c` (the two panels).

**What was built:**

- **`ListView` and `GridView`** are ItemsControls with a kind, as Button and ToggleButton share one object.
  - **Containers:** each item goes in a `ListViewItem` or `GridViewItem`, unless it already is one. The item's visual comes from `ItemTemplate`, the item itself if it is a UIElement, or text.
  - **Panels and scrolling:** a ListView stacks its items in an `ItemsStackPanel`, a GridView wraps them in a horizontal `ItemsWrapGrid`. Both sit in a built-in `ScrollViewer` that scrolls vertically.
  - **Selection** (`ISelector`, `IListViewBase`): `SelectionMode` None, Single, Multiple or Extended; `SelectedIndex`, `SelectedItem`, `SelectedValue` and `SelectedItems`; `SelectionChanged` with `AddedItems` and `RemovedItems`; `SelectAll`.
    - Changes the app makes to `SelectedItems` or `ListViewItem.IsSelected` update the list.
    - A `SelectedIndex` set before the items exist (as markup does) is applied once they do.
    - Items that go away leave the selection.
  - **Clicks:** a click selects in Single mode, toggles in Multiple, and in Extended mode Ctrl toggles and Shift selects a range. With `IsItemClickEnabled`, a click raises `ItemClick` with the `ClickedItem` and doesn't select (except in Multiple mode).
  - **Keyboard:** the list takes the focus, not its items. The arrow keys (Left and Right in a GridView, Up and Down by a row), Home and End move through the items. They select in Single mode and in Extended mode without Ctrl (Shift for a range). Space selects, Enter raises ItemClick, and the item is scrolled into view.
  - **`ScrollIntoView`** with either alignment.
  - **Not supported:** virtualization, incremental loading, `Header` (it is kept but not shown), drag and drop, reordering, swipe, grouping, `ItemContainerStyle`, and the newer `IListViewBase2-6` interfaces.
- **`ListViewItem` and `GridViewItem`** are ContentControls: two more kinds of the shared Button object, with `ISelectorItem.IsSelected`. They draw Windows 10's default looks:
  - **ListViewItem:** 40 px minimum height and 12 px padding; translucent grey under the pointer and while pressed; the accent colour at 60% when selected (80% under the pointer, 90% pressed).
  - **GridViewItem:** a 4 px gap, a grey frame under the pointer, and when selected an accent frame with a check mark in the top-right corner.
  - The item the keyboard is on gets a focus frame while its list has keyboard focus.
- **`ItemsStackPanel`** stacks like a StackPanel. **`ItemsWrapGrid`** gives every item the first item's size (or `ItemWidth`/`ItemHeight`) and wraps rows or columns, at most `MaximumRowsOrColumns` per line. Both report every item as visible.
- **Markup:** the six classes; `SelectionMode`, `SelectedIndex`, `IsItemClickEnabled`, `CanDragItems`, `CanReorderItems` and `IsSwipeEnabled`; `IsSelected` on an item; the panels' `Orientation`, `MaximumRowsOrColumns`, `ItemWidth` and `ItemHeight`.
- **A fix in the markup reader:** `<x:String>`, `<x:Double>` and the like as children of an ItemsControl were dropped. They are added to `Items` boxed as PropertyValues now.

**Tests:**

- **`xamlmarkup`: 221/221 checks pass**, 23 of them new. A ListView of six strings, an Extended ListView of `ListViewItem`s and a Multiple GridView of five tiles check:
  - `SelectedIndex="1"` from markup, `SelectedItem` as the string, and the 60% accent colour;
  - `IsSelected="True"` on a ListViewItem;
  - a click selecting, with one `SelectionChanged` that adds one item and removes one;
  - Down selecting the next item and scrolling it into view, End selecting the last, and `SelectedItem = null` clearing the selection;
  - Ctrl+click adding to an Extended selection, and a plain click selecting just that item;
  - the GridView's four tiles to a row with the fifth wrapping;
  - `ItemClick` with the Border as `ClickedItem`, Multiple's clicks toggling, the accent frame, and `SelectAll`.
- Milestones 1 and 2 still pass 34/34.
