/* Milestone 1 test for wine-uwp's Windows.UI.Xaml: a code-only XAML app (no markup).
 *
 * It does what a C++/CX or C# "App : Application" does: Application.Start, an App object that aggregates
 * Windows.UI.Xaml.Application and overrides OnLaunched, and in OnLaunched a TextBlock as Window.Current.Content.
 * It then checks, from inside the running app, that the API calls behave and that red text was actually drawn
 * in the centre of the window, and exits with 0 only if every check passed.
 *
 * Build: tests/xaml/build.sh (cross-compiled with the Wine build's winegcc and headers).
 * Run:   tests/xaml/run.sh
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "wingdi.h"
#include "winstring.h"
#include "initguid.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"
#define WIDL_using_Windows_ApplicationModel_Activation
#include "windows.applicationmodel.activation.h"
#define WIDL_using_Windows_UI_Text
#define WIDL_using_Windows_UI_Xaml
#define WIDL_using_Windows_UI_Xaml_Controls
#define WIDL_using_Windows_UI_Xaml_Media
#include "windows.ui.xaml.h"

static int failures, checks;

#define check(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
                              else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } fflush(stdout); } while (0)

static const WCHAR hello[] = L"Hello from XAML on Wine";
static BOOL dispatched;
static IUnknown *app_identity;

static HSTRING hs(const WCHAR *s)
{
    HSTRING str;
    WindowsCreateString(s, wcslen(s), &str);
    return str;
}

static void *get_factory(const WCHAR *class_name, REFIID iid)
{
    HSTRING name = hs(class_name);
    void *factory = NULL;
    HRESULT hr = RoGetActivationFactory(name, iid, &factory);
    WindowsDeleteString(name);
    check(hr == S_OK, "factory for %ls, hr %#lx", class_name, hr);
    return factory;
}

static IUnknown *identity(void *iface)
{
    IUnknown *unk;
    IUnknown_QueryInterface((IUnknown *)iface, &IID_IUnknown, (void **)&unk);
    IUnknown_Release(unk);
    return unk;
}

/* ---- CoreDispatcher.RunAsync handler: proves FrameworkView.Run runs dispatcher work ---- */

static HRESULT WINAPI dispatched_QueryInterface(IDispatchedHandler *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IDispatchedHandler))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dispatched_AddRef(IDispatchedHandler *iface) { return 2; }
static ULONG WINAPI dispatched_Release(IDispatchedHandler *iface) { return 1; }
static HRESULT WINAPI dispatched_Invoke(IDispatchedHandler *iface)
{
    dispatched = TRUE;
    return S_OK;
}
static IDispatchedHandlerVtbl dispatched_vtbl =
    { dispatched_QueryInterface, dispatched_AddRef, dispatched_Release, dispatched_Invoke };
static IDispatchedHandler dispatched_handler = { &dispatched_vtbl };

/* ---- the pixel check, run from a timer once the window has painted ---- */

static BOOL CALLBACK find_core_hwnd(HWND hwnd, LPARAM lparam)
{
    WCHAR name[32];
    if (GetClassNameW(hwnd, name, ARRAYSIZE(name)) && !wcscmp(name, L"WineCoreWindow"))
    {
        *(HWND *)lparam = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void CALLBACK verify(HWND unused, UINT msg, UINT_PTR id, DWORD time)
{
    BITMAPINFO info = {{ sizeof(info.bmiHeader) }};
    IApplicationStatics *app_statics;
    IApplication *app;
    int x, y, red = 0, white = 0, minx = INT_MAX, maxx = -1, miny = INT_MAX, maxy = -1, w, h;
    HWND hwnd = NULL;
    DWORD *bits;
    HBITMAP bitmap;
    HDC hdc, mem;
    RECT client;

    KillTimer(NULL, id);
    check(dispatched, "CoreDispatcher.RunAsync callback ran inside the XAML message loop");

    EnumThreadWindows(GetCurrentThreadId(), find_core_hwnd, (LPARAM)&hwnd);
    check(hwnd != NULL, "found the CoreWindow HWND");
    if (hwnd)
    {
        GetClientRect(hwnd, &client);
        w = client.right;
        h = client.bottom;
        hdc = GetDC(hwnd);
        mem = CreateCompatibleDC(hdc);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(mem, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
        SelectObject(mem, bitmap);
        /* repaint into our bitmap through the window procedure, so the check doesn't depend on reading the screen */
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        BitBlt(mem, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
        for (y = 0; y < h; y++) for (x = 0; x < w; x++)
        {
            DWORD p = bits[y * w + x];
            BYTE r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
            if (r > 180 && g < 90 && b < 90)
            {
                red++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
            else if (r > 245 && g > 245 && b > 245) white++;
        }
        printf("client %dx%d, red pixels %d (box %d,%d-%d,%d), white pixels %d\n", w, h, red, minx, miny, maxx, maxy, white);
        check(red > 200, "red text was drawn (%d red pixels)", red);
        check(white > w * h / 2, "light theme background is white");
        if (red)
        {
            int cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
            check(abs(cx - w / 2) < w / 20 && abs(cy - h / 2) < h / 10, "text is centred (centre %d,%d, window %dx%d)", cx, cy, w, h);
            check(maxy - miny > 20 && maxy - miny < 80, "text height %d px fits FontSize 36", maxy - miny);
        }
        if (GetEnvironmentVariableA("XAMLHELLO_SCREENSHOT", NULL, 0))
        {
            /* XAMLHELLO_SCREENSHOT=1 saves what was drawn as xamlhello.bmp in the current directory */
            BITMAPFILEHEADER file = { 0x4d42, sizeof(file) + sizeof(info.bmiHeader) + w * h * 4, 0, 0, sizeof(file) + sizeof(info.bmiHeader) };
            HANDLE f = CreateFileA("xamlhello.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            DWORD written;
            WriteFile(f, &file, sizeof(file), &written, NULL);
            WriteFile(f, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL);
            WriteFile(f, bits, w * h * 4, &written, NULL);
            CloseHandle(f);
        }
        DeleteDC(mem);
        DeleteObject(bitmap);
        ReleaseDC(hwnd, hdc);
    }

    app_statics = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationStatics);
    IApplicationStatics_get_Current(app_statics, &app);
    printf("exiting\n");
    IApplication_Exit(app);
    IApplication_Release(app);
    IApplicationStatics_Release(app_statics);
}

/* ---- the App object: aggregates Windows.UI.Xaml.Application, overrides OnLaunched ---- */

struct app
{
    IApplicationOverrides IApplicationOverrides_iface;
    IInspectable *inner;
    LONG ref;
};

static struct app *impl_from_overrides(IApplicationOverrides *iface)
{
    return CONTAINING_RECORD(iface, struct app, IApplicationOverrides_iface);
}

static HRESULT WINAPI app_QueryInterface(IApplicationOverrides *iface, REFIID iid, void **out)
{
    struct app *app = impl_from_overrides(iface);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IApplicationOverrides))
    {
        *out = &app->IApplicationOverrides_iface;
        InterlockedIncrement(&app->ref);
        return S_OK;
    }
    return IInspectable_QueryInterface(app->inner, iid, out);  /* everything else is the base class */
}

static ULONG WINAPI app_AddRef(IApplicationOverrides *iface)
{
    return InterlockedIncrement(&impl_from_overrides(iface)->ref);
}

static ULONG WINAPI app_Release(IApplicationOverrides *iface)
{
    struct app *app = impl_from_overrides(iface);
    ULONG ref = InterlockedDecrement(&app->ref);
    if (!ref)
    {
        printf("App object destroyed\n");
        IInspectable_Release(app->inner);
        free(app);
    }
    return ref;
}

static HRESULT WINAPI app_GetIids(IApplicationOverrides *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI app_GetRuntimeClassName(IApplicationOverrides *iface, HSTRING *name) { *name = hs(L"XamlHello.App"); return S_OK; }
static HRESULT WINAPI app_GetTrustLevel(IApplicationOverrides *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }

static HRESULT WINAPI app_OnActivated(IApplicationOverrides *iface, IActivatedEventArgs *args)
{
    check(0, "OnActivated called instead of OnLaunched");
    return S_OK;
}

static HRESULT WINAPI app_OnLaunched(IApplicationOverrides *iface, ILaunchActivatedEventArgs *args)
{
    ISolidColorBrushFactory *brush_factory;
    IApplicationStatics *app_statics;
    IActivationFactory *tb_factory;
    IWindowStatics *window_statics;
    IFrameworkElement *fe;
    ISolidColorBrush *red;
    IInspectable *object;
    IUIElement *element, *content;
    ICoreDispatcher *dispatcher;
    IAsyncAction *action;
    IApplication *current;
    ITextBlock *text;
    IBrush *brush;
    IWindow *window;
    HSTRING str;
    Color color = { 255, 220, 20, 20 };
    Rect bounds;
    HRESULT hr;

    printf("OnLaunched, args %p\n", args);
    check(args != NULL, "OnLaunched got LaunchActivatedEventArgs");

    app_statics = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationStatics);
    hr = IApplicationStatics_get_Current(app_statics, &current);
    check(hr == S_OK && current && identity(current) == app_identity, "Application.Current is the App object");
    if (current) IApplication_Release(current);
    IApplicationStatics_Release(app_statics);

    window_statics = get_factory(L"Windows.UI.Xaml.Window", &IID_IWindowStatics);
    hr = IWindowStatics_get_Current(window_statics, &window);
    check(hr == S_OK && window, "Window.Current exists, hr %#lx", hr);
    IWindowStatics_Release(window_statics);
    if (!window) return E_FAIL;

    tb_factory = get_factory(L"Windows.UI.Xaml.Controls.TextBlock", &IID_IActivationFactory);
    hr = IActivationFactory_ActivateInstance(tb_factory, &object);
    check(hr == S_OK, "new TextBlock, hr %#lx", hr);
    IActivationFactory_Release(tb_factory);
    IInspectable_GetRuntimeClassName(object, &str);
    check(!wcscmp(WindowsGetStringRawBuffer(str, NULL), L"Windows.UI.Xaml.Controls.TextBlock"), "TextBlock runtime class name");
    WindowsDeleteString(str);

    IInspectable_QueryInterface(object, &IID_ITextBlock, (void **)&text);
    str = hs(hello);
    ITextBlock_put_Text(text, str);
    WindowsDeleteString(str);
    ITextBlock_get_Text(text, &str);
    check(!wcscmp(WindowsGetStringRawBuffer(str, NULL), hello), "TextBlock.Text round-trips");
    WindowsDeleteString(str);
    ITextBlock_put_FontSize(text, 36.0);

    brush_factory = get_factory(L"Windows.UI.Xaml.Media.SolidColorBrush", &IID_ISolidColorBrushFactory);
    hr = ISolidColorBrushFactory_CreateInstanceWithColor(brush_factory, color, &red);
    check(hr == S_OK, "new SolidColorBrush(red), hr %#lx", hr);
    ISolidColorBrushFactory_Release(brush_factory);
    ISolidColorBrush_QueryInterface(red, &IID_IBrush, (void **)&brush);
    ITextBlock_put_Foreground(text, brush);
    IBrush_Release(brush);
    ISolidColorBrush_Release(red);

    hr = IInspectable_QueryInterface(object, &IID_IFrameworkElement, (void **)&fe);
    check(hr == S_OK, "TextBlock is a FrameworkElement");
    IFrameworkElement_put_HorizontalAlignment(fe, HorizontalAlignment_Center);
    IFrameworkElement_put_VerticalAlignment(fe, VerticalAlignment_Center);
    IFrameworkElement_Release(fe);

    hr = IInspectable_QueryInterface(object, &IID_IUIElement, (void **)&element);
    check(hr == S_OK, "TextBlock is a UIElement");
    hr = IWindow_put_Content(window, element);
    check(hr == S_OK, "Window.Content = TextBlock, hr %#lx", hr);
    IWindow_get_Content(window, &content);
    check(content && identity(content) == identity(element), "Window.Content round-trips");
    if (content) IUIElement_Release(content);
    IUIElement_Release(element);
    ITextBlock_Release(text);
    IInspectable_Release(object);

    hr = IWindow_Activate(window);
    check(hr == S_OK, "Window.Activate, hr %#lx", hr);
    IWindow_get_Bounds(window, &bounds);
    check(bounds.Width > 100 && bounds.Height > 100, "Window.Bounds %.0fx%.0f", bounds.Width, bounds.Height);

    hr = IWindow_get_Dispatcher(window, &dispatcher);
    check(hr == S_OK, "Window.Dispatcher");
    hr = ICoreDispatcher_RunAsync(dispatcher, CoreDispatcherPriority_Normal, &dispatched_handler, &action);
    check(hr == S_OK, "CoreDispatcher.RunAsync, hr %#lx", hr);
    if (SUCCEEDED(hr)) IAsyncAction_Release(action);
    ICoreDispatcher_Release(dispatcher);
    IWindow_Release(window);

    SetTimer(NULL, 0, 1500, verify);
    return S_OK;
}

static HRESULT WINAPI app_OnFileActivated(IApplicationOverrides *iface, IFileActivatedEventArgs *args) { return S_OK; }
static HRESULT WINAPI app_OnSearchActivated(IApplicationOverrides *iface, ISearchActivatedEventArgs *args) { return S_OK; }
static HRESULT WINAPI app_OnShareTargetActivated(IApplicationOverrides *iface, IShareTargetActivatedEventArgs *args) { return S_OK; }
static HRESULT WINAPI app_OnFileOpenPickerActivated(IApplicationOverrides *iface, IFileOpenPickerActivatedEventArgs *args) { return S_OK; }
static HRESULT WINAPI app_OnFileSavePickerActivated(IApplicationOverrides *iface, IFileSavePickerActivatedEventArgs *args) { return S_OK; }
static HRESULT WINAPI app_OnCachedFileUpdaterActivated(IApplicationOverrides *iface, ICachedFileUpdaterActivatedEventArgs *args) { return S_OK; }
static HRESULT WINAPI app_OnWindowCreated(IApplicationOverrides *iface, IWindowCreatedEventArgs *args) { return S_OK; }

static IApplicationOverridesVtbl app_vtbl =
{
    app_QueryInterface, app_AddRef, app_Release, app_GetIids, app_GetRuntimeClassName, app_GetTrustLevel,
    app_OnActivated, app_OnLaunched, app_OnFileActivated, app_OnSearchActivated, app_OnShareTargetActivated,
    app_OnFileOpenPickerActivated, app_OnFileSavePickerActivated, app_OnCachedFileUpdaterActivated, app_OnWindowCreated,
};

/* ---- Application.Start callback: "ref new App()" ---- */

static HRESULT WINAPI init_QueryInterface(IApplicationInitializationCallback *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IApplicationInitializationCallback))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI init_AddRef(IApplicationInitializationCallback *iface) { return 2; }
static ULONG WINAPI init_Release(IApplicationInitializationCallback *iface) { return 1; }

static HRESULT WINAPI init_Invoke(IApplicationInitializationCallback *iface, IApplicationInitializationCallbackParams *params)
{
    IApplicationFactory *factory;
    IApplication *base;
    IApplicationOverrides *overrides;
    struct app *app;
    HRESULT hr;

    printf("ApplicationInitializationCallback\n");
    check(params != NULL, "callback params");
    factory = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationFactory);
    app = calloc(1, sizeof(*app));
    app->IApplicationOverrides_iface.lpVtbl = &app_vtbl;
    app->ref = 1;
    hr = IApplicationFactory_CreateInstance(factory, (IInspectable *)&app->IApplicationOverrides_iface, &app->inner, &base);
    check(hr == S_OK && app->inner && base, "IApplicationFactory::CreateInstance with an outer object, hr %#lx", hr);
    IApplicationFactory_Release(factory);
    app_identity = (IUnknown *)&app->IApplicationOverrides_iface;
    check(identity(base) == app_identity, "the base Application delegates identity to the App object");

    /* what the generated App constructor does */
    hr = IApplication_put_RequestedTheme(base, ApplicationTheme_Light);
    check(hr == S_OK, "Application.RequestedTheme = Light");
    hr = IApplication_QueryInterface(base, &IID_IApplicationOverrides, (void **)&overrides);
    check(hr == S_OK && overrides == &app->IApplicationOverrides_iface, "IApplicationOverrides comes from the App object");
    if (overrides) IApplicationOverrides_Release(overrides);
    IApplication_Release(base);
    /* like "ref new App();", drop our reference: the framework keeps the app alive */
    IApplicationOverrides_Release(&app->IApplicationOverrides_iface);
    return S_OK;
}

static IApplicationInitializationCallbackVtbl init_vtbl = { init_QueryInterface, init_AddRef, init_Release, init_Invoke };
static IApplicationInitializationCallback init_callback = { &init_vtbl };

int main(void)
{
    IApplicationStatics *statics;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_SINGLETHREADED);
    statics = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationStatics);
    if (!statics) return 2;
    printf("Application.Start\n");
    hr = IApplicationStatics_Start(statics, &init_callback);
    check(hr == S_OK, "Application.Start returned after the app exited, hr %#lx", hr);
    IApplicationStatics_Release(statics);
    printf("%d checks, %d failures: %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
