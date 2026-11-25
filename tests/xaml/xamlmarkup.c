/* Milestone 3 test for wine-uwp's Windows.UI.Xaml: an app built the way Visual Studio's "Blank App" template is,
 * with its UI in compiled markup (App.xbf, MainPage.xbf, made by scripts/xbf.py from tests/xaml/markup/).
 *
 * Like the code Visual Studio generates for C++/CX:
 *   - App aggregates Application, implements IXamlMetadataProvider, and its constructor calls
 *     Application.LoadComponent(this, "ms-appx:///App.xaml") (RequestedTheme, Application.Resources).
 *   - OnLaunched reads PrelaunchActivated, creates a Frame as Window.Content and navigates it to MainPage by
 *     TypeName, so the framework creates the page through the metadata provider.
 *   - MainPage aggregates Page, implements IComponentConnector, and its constructor calls
 *     LoadComponent(this, "ms-appx:///MainPage.xaml"); Connect() receives the x:Name elements and the Click handler.
 * Then it checks what was built and drawn, clicks the button, and loads Microsoft's own compiler-made test XBFs
 * (if present next to the exe, in ms\) to check the reader on real compiler output.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"
#define WIDL_using_Windows_ApplicationModel_Activation
#include "windows.applicationmodel.activation.h"
#define WIDL_using_Windows_UI_Text
#define WIDL_using_Windows_UI_Xaml
#define WIDL_using_Windows_UI_Xaml_Controls
#define WIDL_using_Windows_UI_Xaml_Controls_Primitives
#define WIDL_using_Windows_UI_Xaml_Media
#define WIDL_using_Windows_UI_Xaml_Markup
#define WIDL_using_Windows_UI_Xaml_Interop
#include "windows.ui.xaml.h"

/* Windows.ApplicationModel.Activation.IPrelaunchActivatedEventArgs, not in Wine's headers */
DEFINE_GUID(IID_IPrelaunchActivatedEventArgs, 0x0c44717b, 0x19f7, 0x48d6, 0xb0, 0x46, 0xcf, 0x22, 0x82, 0x6e, 0xaa, 0x74);
typedef struct IPrelaunchActivatedEventArgs { const struct IPrelaunchActivatedEventArgsVtbl *lpVtbl; } IPrelaunchActivatedEventArgs;
struct IPrelaunchActivatedEventArgsVtbl
{
    HRESULT (WINAPI *QueryInterface)(IPrelaunchActivatedEventArgs *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IPrelaunchActivatedEventArgs *);
    ULONG (WINAPI *Release)(IPrelaunchActivatedEventArgs *);
    HRESULT (WINAPI *GetIids)(IPrelaunchActivatedEventArgs *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IPrelaunchActivatedEventArgs *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IPrelaunchActivatedEventArgs *, TrustLevel *);
    HRESULT (WINAPI *get_PrelaunchActivated)(IPrelaunchActivatedEventArgs *, boolean *);
};
#define IPrelaunchActivatedEventArgs_get_PrelaunchActivated(p, v) (p)->lpVtbl->get_PrelaunchActivated(p, v)
#define IPrelaunchActivatedEventArgs_Release(p) (p)->lpVtbl->Release(p)

static int failures, checks;

#define check(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
                              else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } fflush(stdout); } while (0)

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
    if (hr != S_OK) check(0, "factory for %ls, hr %#lx", class_name, hr);
    return factory;
}

static HRESULT load_component(IInspectable *component, const WCHAR *uri_string)
{
    IApplicationStatics *statics = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationStatics);
    IUriRuntimeClassFactory *uri_factory = get_factory(L"Windows.Foundation.Uri", &IID_IUriRuntimeClassFactory);
    IUriRuntimeClass *uri;
    HSTRING str = hs(uri_string);
    HRESULT hr;

    IUriRuntimeClassFactory_CreateUri(uri_factory, str, &uri);
    WindowsDeleteString(str);
    hr = IApplicationStatics_LoadComponent(statics, component, uri);
    IUriRuntimeClass_Release(uri);
    IUriRuntimeClassFactory_Release(uri_factory);
    IApplicationStatics_Release(statics);
    return hr;
}

static const WCHAR *text_of(ITextBlock *tb, WCHAR *buf, int len)
{
    HSTRING str;
    ITextBlock_get_Text(tb, &str);
    lstrcpynW(buf, WindowsGetStringRawBuffer(str, NULL), len);
    WindowsDeleteString(str);
    return buf;
}

/* ---- MainPage: aggregates Page, implements IComponentConnector ---- */

struct main_page
{
    IComponentConnector IComponentConnector_iface;
    IInspectable *inner;
    LONG ref;
    ITextBlock *title;          /* x:Name="Title" */
    ITextBlock *styled;         /* x:Name="Styled" */
    IButton *templated;         /* x:Name="TemplatedButton" */
    IButton *click_button;      /* x:Name="ClickButton" */
    int clicks;
};

static struct main_page *the_page;

static struct main_page *page_from_connector(IComponentConnector *iface)
{
    return CONTAINING_RECORD(iface, struct main_page, IComponentConnector_iface);
}

static HRESULT WINAPI page_QueryInterface(IComponentConnector *iface, REFIID iid, void **out)
{
    struct main_page *page = page_from_connector(iface);
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IComponentConnector))
    {
        *out = iface;
        InterlockedIncrement(&page->ref);
        return S_OK;
    }
    return IInspectable_QueryInterface(page->inner, iid, out);
}
static ULONG WINAPI page_AddRef(IComponentConnector *iface) { return InterlockedIncrement(&page_from_connector(iface)->ref); }
static ULONG WINAPI page_Release(IComponentConnector *iface)
{
    struct main_page *page = page_from_connector(iface);
    ULONG ref = InterlockedDecrement(&page->ref);
    if (!ref)
    {
        if (page->title) ITextBlock_Release(page->title);
        if (page->styled) ITextBlock_Release(page->styled);
        if (page->templated) IButton_Release(page->templated);
        if (page->click_button) IButton_Release(page->click_button);
        IInspectable_Release(page->inner);
        if (the_page == page) the_page = NULL;
        free(page);
    }
    return ref;
}
static HRESULT WINAPI page_GetIids(IComponentConnector *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI page_GetRuntimeClassName(IComponentConnector *iface, HSTRING *name) { *name = hs(L"XamlMarkup.MainPage"); return S_OK; }
static HRESULT WINAPI page_GetTrustLevel(IComponentConnector *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }

/* the Click handler "OnClick" */
static HRESULT WINAPI click_QueryInterface(IRoutedEventHandler *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IRoutedEventHandler))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI click_AddRef(IRoutedEventHandler *iface) { return 2; }
static ULONG WINAPI click_Release(IRoutedEventHandler *iface) { return 1; }
static HRESULT WINAPI click_Invoke(IRoutedEventHandler *iface, IInspectable *sender, IRoutedEventArgs *args)
{
    HSTRING str;
    if (!the_page) return S_OK;
    the_page->clicks++;
    str = hs(L"Clicked!");
    ITextBlock_put_Text(the_page->title, str);
    WindowsDeleteString(str);
    return S_OK;
}
static IRoutedEventHandlerVtbl click_vtbl = { click_QueryInterface, click_AddRef, click_Release, click_Invoke };
static IRoutedEventHandler on_click = { &click_vtbl };

static HRESULT WINAPI page_Connect(IComponentConnector *iface, INT32 id, IInspectable *target)
{
    struct main_page *page = page_from_connector(iface);
    IButtonBase *base;
    EventRegistrationToken token;

    printf("Connect(%d, %p)\n", id, target);
    switch (id)
    {
    case 1:
        IInspectable_QueryInterface(target, &IID_ITextBlock, (void **)&page->title);
        break;
    case 2:
        IInspectable_QueryInterface(target, &IID_IButton, (void **)&page->click_button);
        IInspectable_QueryInterface(target, &IID_IButtonBase, (void **)&base);
        IButtonBase_add_Click(base, &on_click, &token);
        IButtonBase_Release(base);
        break;
    case 3:
        IInspectable_QueryInterface(target, &IID_ITextBlock, (void **)&page->styled);
        break;
    case 4:
        IInspectable_QueryInterface(target, &IID_IButton, (void **)&page->templated);
        break;
    }
    return S_OK;
}

static IComponentConnectorVtbl page_vtbl =
{
    page_QueryInterface, page_AddRef, page_Release, page_GetIids, page_GetRuntimeClassName, page_GetTrustLevel, page_Connect,
};

static IInspectable *create_main_page(void)
{
    IPageFactory *factory = get_factory(L"Windows.UI.Xaml.Controls.Page", &IID_IPageFactory);
    struct main_page *page = calloc(1, sizeof(*page));
    IPage *base;
    HRESULT hr;

    page->IComponentConnector_iface.lpVtbl = &page_vtbl;
    page->ref = 1;
    hr = IPageFactory_CreateInstance(factory, (IInspectable *)&page->IComponentConnector_iface, &page->inner, &base);
    check(hr == S_OK, "MainPage aggregates Page, hr %#lx", hr);
    IPage_Release(base);
    IPageFactory_Release(factory);
    the_page = page;
    /* MainPage::InitializeComponent() */
    hr = load_component((IInspectable *)&page->IComponentConnector_iface, L"ms-appx:///MainPage.xaml");
    check(hr == S_OK, "LoadComponent(MainPage.xaml), hr %#lx", hr);
    return (IInspectable *)&page->IComponentConnector_iface;
}

/* ---- the IXamlType for MainPage ---- */

static HRESULT WINAPI type_QueryInterface(IXamlType *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IXamlType))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI type_AddRef(IXamlType *iface) { return 2; }
static ULONG WINAPI type_Release(IXamlType *iface) { return 1; }
static HRESULT WINAPI type_GetIids(IXamlType *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI type_GetRuntimeClassName(IXamlType *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI type_GetTrustLevel(IXamlType *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI type_notimpl(IXamlType *iface) { return E_NOTIMPL; }
static HRESULT WINAPI type_get_FullName(IXamlType *iface, HSTRING *value) { *value = hs(L"XamlMarkup.MainPage"); return S_OK; }
static HRESULT WINAPI type_ActivateInstance(IXamlType *iface, IInspectable **value)
{
    printf("IXamlType::ActivateInstance(MainPage)\n");
    *value = create_main_page();
    return S_OK;
}

static IXamlTypeVtbl type_vtbl =
{
    type_QueryInterface, type_AddRef, type_Release, type_GetIids, type_GetRuntimeClassName, type_GetTrustLevel,
    (void *)type_notimpl, (void *)type_notimpl, type_get_FullName, (void *)type_notimpl, (void *)type_notimpl,
    (void *)type_notimpl, (void *)type_notimpl, (void *)type_notimpl, (void *)type_notimpl, (void *)type_notimpl,
    (void *)type_notimpl, (void *)type_notimpl, type_ActivateInstance, (void *)type_notimpl, (void *)type_notimpl,
    (void *)type_notimpl, (void *)type_notimpl, (void *)type_notimpl,
};
static IXamlType main_page_type = { &type_vtbl };
static int metadata_queries;

/* ---- App: aggregates Application, overrides OnLaunched, implements IXamlMetadataProvider ---- */

struct app
{
    IApplicationOverrides IApplicationOverrides_iface;
    IXamlMetadataProvider IXamlMetadataProvider_iface;
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
        *out = &app->IApplicationOverrides_iface;
    else if (IsEqualGUID(iid, &IID_IXamlMetadataProvider))
        *out = &app->IXamlMetadataProvider_iface;
    else return IInspectable_QueryInterface(app->inner, iid, out);
    InterlockedIncrement(&app->ref);
    return S_OK;
}
static ULONG WINAPI app_AddRef(IApplicationOverrides *iface) { return InterlockedIncrement(&impl_from_overrides(iface)->ref); }
static ULONG WINAPI app_Release(IApplicationOverrides *iface)
{
    struct app *app = impl_from_overrides(iface);
    ULONG ref = InterlockedDecrement(&app->ref);
    if (!ref) { IInspectable_Release(app->inner); free(app); }
    return ref;
}
static HRESULT WINAPI app_GetIids(IApplicationOverrides *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI app_GetRuntimeClassName(IApplicationOverrides *iface, HSTRING *name) { *name = hs(L"XamlMarkup.App"); return S_OK; }
static HRESULT WINAPI app_GetTrustLevel(IApplicationOverrides *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI app_OnActivated(IApplicationOverrides *iface, IActivatedEventArgs *args) { return S_OK; }

static void CALLBACK verify(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);

static HRESULT WINAPI app_OnLaunched(IApplicationOverrides *iface, ILaunchActivatedEventArgs *args)
{
    IWindowStatics *window_statics = get_factory(L"Windows.UI.Xaml.Window", &IID_IWindowStatics);
    IFrameFactory *frame_factory = get_factory(L"Windows.UI.Xaml.Controls.Frame", &IID_IFrameFactory);
    IPrelaunchActivatedEventArgs *prelaunch;
    IInspectable *inner;
    IUIElement *content, *ui;
    IWindow *window;
    IFrame *frame;
    TypeName type;
    boolean prelaunched = TRUE, navigated = FALSE;
    HSTRING arguments;
    HRESULT hr;

    /* the template: if (e->PrelaunchActivated == false) { ... } */
    hr = ILaunchActivatedEventArgs_QueryInterface(args, &IID_IPrelaunchActivatedEventArgs, (void **)&prelaunch);
    check(hr == S_OK, "LaunchActivatedEventArgs has IPrelaunchActivatedEventArgs, hr %#lx", hr);
    if (SUCCEEDED(hr))
    {
        IPrelaunchActivatedEventArgs_get_PrelaunchActivated(prelaunch, &prelaunched);
        IPrelaunchActivatedEventArgs_Release(prelaunch);
    }
    check(!prelaunched, "PrelaunchActivated is false");

    IWindowStatics_get_Current(window_statics, &window);
    IWindowStatics_Release(window_statics);
    IWindow_get_Content(window, &content);
    check(!content, "Window.Content starts empty");

    hr = IFrameFactory_CreateInstance(frame_factory, NULL, &inner, &frame);
    check(hr == S_OK && frame, "new Frame, hr %#lx", hr);
    IFrameFactory_Release(frame_factory);
    IFrame_QueryInterface(frame, &IID_IUIElement, (void **)&ui);
    IWindow_put_Content(window, ui);
    IUIElement_Release(ui);

    ILaunchActivatedEventArgs_get_Arguments(args, &arguments);
    type.Name = hs(L"XamlMarkup.MainPage");
    type.Kind = TypeKind_Custom;
    hr = IFrame_Navigate(frame, type, NULL, &navigated);
    check(hr == S_OK && navigated, "Frame.Navigate(typeof(MainPage)), hr %#lx", hr);
    WindowsDeleteString(type.Name);
    WindowsDeleteString(arguments);
    check(metadata_queries > 0, "the frame asked the app's IXamlMetadataProvider for MainPage (%d)", metadata_queries);
    IFrame_Release(frame);

    IWindow_Activate(window);
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

/* IXamlMetadataProvider, as XamlTypeInfo.g.cpp provides it */
static struct app *impl_from_provider(IXamlMetadataProvider *iface)
{
    return CONTAINING_RECORD(iface, struct app, IXamlMetadataProvider_iface);
}
static HRESULT WINAPI provider_QueryInterface(IXamlMetadataProvider *iface, REFIID iid, void **out)
{
    return app_QueryInterface(&impl_from_provider(iface)->IApplicationOverrides_iface, iid, out);
}
static ULONG WINAPI provider_AddRef(IXamlMetadataProvider *iface) { return app_AddRef(&impl_from_provider(iface)->IApplicationOverrides_iface); }
static ULONG WINAPI provider_Release(IXamlMetadataProvider *iface) { return app_Release(&impl_from_provider(iface)->IApplicationOverrides_iface); }
static HRESULT WINAPI provider_GetIids(IXamlMetadataProvider *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI provider_GetRuntimeClassName(IXamlMetadataProvider *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI provider_GetTrustLevel(IXamlMetadataProvider *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI provider_GetXamlTypeByFullName(IXamlMetadataProvider *iface, HSTRING name, IXamlType **value)
{
    metadata_queries++;
    printf("GetXamlType(%ls)\n", WindowsGetStringRawBuffer(name, NULL));
    *value = NULL;
    if (wcscmp(WindowsGetStringRawBuffer(name, NULL), L"XamlMarkup.MainPage")) return S_OK;
    *value = &main_page_type;
    return S_OK;
}
static HRESULT WINAPI provider_GetXamlType(IXamlMetadataProvider *iface, TypeName type, IXamlType **value)
{
    return provider_GetXamlTypeByFullName(iface, type.Name, value);
}
static HRESULT WINAPI provider_GetXmlnsDefinitions(IXamlMetadataProvider *iface, UINT32 *count, XmlnsDefinition **defs)
{
    *count = 0;
    *defs = NULL;
    return S_OK;
}
static IXamlMetadataProviderVtbl provider_vtbl =
{
    provider_QueryInterface, provider_AddRef, provider_Release, provider_GetIids, provider_GetRuntimeClassName,
    provider_GetTrustLevel, provider_GetXamlType, provider_GetXamlTypeByFullName, provider_GetXmlnsDefinitions,
};

/* ---- checks ---- */

static HWND hwnd;
static DWORD *pixels;
static int width, height;

static BOOL CALLBACK find_core_hwnd(HWND h, LPARAM lparam)
{
    WCHAR name[32];
    if (GetClassNameW(h, name, ARRAYSIZE(name)) && !wcscmp(name, L"WineCoreWindow"))
    {
        *(HWND *)lparam = h;
        return FALSE;
    }
    return TRUE;
}

static void snapshot(void)
{
    BITMAPINFO info = {{ sizeof(info.bmiHeader) }};
    HDC hdc, mem;
    HBITMAP bitmap;
    RECT client;
    DWORD *bits;

    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    GetClientRect(hwnd, &client);
    width = client.right;
    height = client.bottom;
    hdc = GetDC(hwnd);
    mem = CreateCompatibleDC(hdc);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(mem, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    SelectObject(mem, bitmap);
    BitBlt(mem, 0, 0, width, height, hdc, 0, 0, SRCCOPY);
    free(pixels);
    pixels = malloc(width * height * 4);
    memcpy(pixels, bits, width * height * 4);
    if (GetEnvironmentVariableA("XAMLHELLO_SCREENSHOT", NULL, 0))
    {
        BITMAPFILEHEADER file = { 0x4d42, sizeof(file) + sizeof(info.bmiHeader) + width * height * 4, 0, 0, sizeof(file) + sizeof(info.bmiHeader) };
        HANDLE f = CreateFileA("xamlmarkup.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD written;
        WriteFile(f, &file, sizeof(file), &written, NULL);
        WriteFile(f, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL);
        WriteFile(f, bits, width * height * 4, &written, NULL);
        CloseHandle(f);
    }
    DeleteDC(mem);
    DeleteObject(bitmap);
    ReleaseDC(hwnd, hdc);
}

static int count_color(DWORD rgb)
{
    int i, n = 0;
    for (i = 0; i < width * height; i++) if ((pixels[i] & 0xffffff) == rgb) n++;
    return n;
}

static double actual(void *element, BOOL want_height)
{
    IFrameworkElement *fe;
    DOUBLE v = -1;
    if (FAILED(IUnknown_QueryInterface((IUnknown *)element, &IID_IFrameworkElement, (void **)&fe))) return -1;
    if (want_height) IFrameworkElement_get_ActualHeight(fe, &v);
    else IFrameworkElement_get_ActualWidth(fe, &v);
    IFrameworkElement_Release(fe);
    return v;
}

static BOOL brush_color(IBrush *brush, UINT32 *argb)
{
    ISolidColorBrush *solid;
    Color c;
    if (!brush || FAILED(IBrush_QueryInterface(brush, &IID_ISolidColorBrush, (void **)&solid))) return FALSE;
    ISolidColorBrush_get_Color(solid, &c);
    ISolidColorBrush_Release(solid);
    *argb = c.A << 24 | c.R << 16 | c.G << 8 | c.B;
    return TRUE;
}

/* milestone 4: styles, theme and merged dictionaries */
static void check_styles(struct main_page *page)
{
    IControl *control;
    IFrameworkElement *fe;
    IStyle *style, *based_on;
    IBrush *brush;
    Thickness padding;
    TypeName target;
    UINT32 argb = 0;
    DOUBLE size;

    check(page->styled != NULL, "Connect(3) gave us x:Name=\"Styled\"");
    if (!page->styled) return;

    /* implicit TextBlock style from the merged Styles.xaml */
    ITextBlock_get_FontSize(page->styled, &size);
    check(size == 18, "the implicit TextBlock style set FontSize 18 (%.1f)", size);
    ITextBlock_get_Foreground(page->styled, &brush);
    check(brush_color(brush, &argb) && argb == 0xff2b5797, "and Foreground #FF2B5797 (%08x)", argb);
    if (brush) IBrush_Release(brush);
    /* a local value beats the style */
    ITextBlock_get_FontSize(page->title, &size);
    check(size == 28, "Title keeps its local FontSize 28 over the implicit style (%.1f)", size);
    ITextBlock_get_Foreground(page->title, &brush);
    check(brush_color(brush, &argb) && argb == 0xff2b5797, "Title takes the style's Foreground, which it doesn't set (%08x)", argb);
    if (brush) IBrush_Release(brush);

    /* Style="{StaticResource AccentButton}", BasedOn BaseButton */
    IButton_QueryInterface(page->click_button, &IID_IFrameworkElement, (void **)&fe);
    IFrameworkElement_get_Style(fe, &style);
    check(style != NULL, "ClickButton.Style is set");
    if (style)
    {
        IStyle_get_TargetType(style, &target);
        check(!wcscmp(WindowsGetStringRawBuffer(target.Name, NULL), L"Button"), "Style.TargetType is Button (%ls)",
              WindowsGetStringRawBuffer(target.Name, NULL));
        WindowsDeleteString(target.Name);
        IStyle_get_BasedOn(style, &based_on);
        check(based_on != NULL, "AccentButton is BasedOn BaseButton");
        if (based_on) IStyle_Release(based_on);
        IStyle_Release(style);
    }
    IFrameworkElement_Release(fe);
    IButton_QueryInterface(page->click_button, &IID_IControl, (void **)&control);
    IControl_get_Background(control, &brush);
    check(brush_color(brush, &argb) && argb == 0xffc50f1f, "Background from the style's {StaticResource StyleAccentBrush} (%08x)", argb);
    if (brush) IBrush_Release(brush);
    IControl_get_Foreground(control, &brush);
    check(brush_color(brush, &argb) && argb == 0xffffffff, "Foreground White from AccentButton (%08x)", argb);
    if (brush) IBrush_Release(brush);
    IControl_get_Padding(control, &padding);
    check(padding.Left == 24 && padding.Top == 10, "Padding 24,10 from BaseButton (%.0f,%.0f)", padding.Left, padding.Top);
    IControl_get_FontSize(control, &size);
    check(size == 16, "FontSize 16 from BaseButton (%.1f)", size);
    IControl_Release(control);
}

/* the bounding box of a colour in the last snapshot */
static int find_color(DWORD rgb, RECT *box)
{
    int x, y, n = 0;
    SetRect(box, INT_MAX, INT_MAX, -1, -1);
    for (y = 0; y < height; y++) for (x = 0; x < width; x++)
    {
        if ((pixels[y * width + x] & 0xffffff) != rgb) continue;
        n++;
        box->left = min(box->left, x); box->top = min(box->top, y);
        box->right = max(box->right, x); box->bottom = max(box->bottom, y);
    }
    return n;
}

/* milestone 4 part 2: a control template with visual states */
static void check_template(struct main_page *page)
{
    RECT box, frame;
    int n, cx, cy, white;

    check(page->templated != NULL, "Connect(4) gave us x:Name=\"TemplatedButton\"");
    if (!page->templated) return;
    snapshot();
    n = find_color(0x107c10, &box);
    check(n > 1500, "the template's Grid draws {TemplateBinding Background} #107C10 (%d px)", n);
    if (n <= 0) return;
    cx = (box.left + box.right) / 2;
    cy = (box.top + box.bottom) / 2;
    {
        int x, y;
        for (white = 0, y = box.top; y <= box.bottom; y++) for (x = box.left; x <= box.right; x++)
            if ((pixels[y * width + x] & 0xffffff) == 0xffffff) white++;
    }
    check(white > 30, "the ContentPresenter draws the button's Content in its {TemplateBinding Foreground} white (%d px)", white);
    check(box.right - box.left > 80 && box.bottom - box.top > 30, "the template is sized by the presenter's padding 16,8 (%ldx%ld)",
          box.right - box.left + 1, box.bottom - box.top + 1);

    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(cx, cy));
    snapshot();
    check(find_color(0x0b5a0b, &box) > 1500 && find_color(0x107c10, &box) == 0,
          "PointerOver: the state's Setter changes RootGrid.Background to #0B5A0B");

    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
    snapshot();
    check(find_color(0x052e05, &box) > 1000, "Pressed: ObjectAnimationUsingKeyFrames sets RootGrid.Background #052E05 (%d px)",
          find_color(0x052e05, &box));
    n = find_color(0xffd700, &frame);
    check(n > 200, "Pressed: ColorAnimation turns Frame's BorderBrush gold (%d px)", n);

    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
    snapshot();
    check(find_color(0xffd700, &frame) == 0 && find_color(0x0b5a0b, &box) > 1500,
          "back to PointerOver: the Pressed values are restored (border transparent) and the hover colour is back");

    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(cx, cy - 200 > 0 ? 5 : 5));
    SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
    snapshot();
    check(find_color(0x107c10, &box) > 1500 && find_color(0x0b5a0b, &box) == 0, "Normal again: the original background is restored");
}

static void check_microsoft_xbf(const WCHAR *file, const WCHAR *component_class)
{
    IActivationFactory *factory;
    IInspectable *component;
    WCHAR uri[MAX_PATH], path[MAX_PATH];
    HRESULT hr;

    swprintf(path, ARRAYSIZE(path), L"ms\\%s.xbf", file);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    {
        printf("skipped: %ls (not present)\n", path);
        return;
    }
    if (!(factory = get_factory(component_class, &IID_IActivationFactory))) return;
    IActivationFactory_ActivateInstance(factory, &component);
    IActivationFactory_Release(factory);
    swprintf(uri, ARRAYSIZE(uri), L"ms-appx:///ms/%s.xaml", file);
    hr = load_component(component, uri);
    check(hr == S_OK, "Microsoft-compiled %ls.xbf loads into a %ls, hr %#lx", file, component_class, hr);
    IInspectable_Release(component);
}

static void CALLBACK verify(HWND unused, UINT msg, UINT_PTR id, DWORD time)
{
    IApplicationStatics *app_statics;
    IApplication *app;
    IResourceDictionary *dict;
    IMap_IInspectable_IInspectable *map;
    IPropertyValueStatics *pv;
    IInspectable *key, *value;
    ISolidColorBrush *brush;
    ApplicationTheme theme;
    Color color;
    WCHAR buf[64];
    double tx, ty, bw, bh;
    unsigned int size;
    HRESULT hr;

    KillTimer(NULL, id);
    EnumThreadWindows(GetCurrentThreadId(), find_core_hwnd, (LPARAM)&hwnd);

    /* App.xaml */
    app_statics = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationStatics);
    IApplicationStatics_get_Current(app_statics, &app);
    IApplication_get_RequestedTheme(app, &theme);
    check(theme == ApplicationTheme_Light, "App.xaml set RequestedTheme=Light (%d)", theme);
    IApplication_get_Resources(app, &dict);
    IResourceDictionary_QueryInterface(dict, &IID_IMap_IInspectable_IInspectable, (void **)&map);
    IMap_IInspectable_IInspectable_get_Size(map, &size);
    check(size == 2, "Application.Resources has the 2 brushes from App.xaml (%u)", size);
    pv = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
    {
        HSTRING s = hs(L"AccentBrush");
        IPropertyValueStatics_CreateString(pv, s, &key);
        WindowsDeleteString(s);
    }
    hr = IMap_IInspectable_IInspectable_Lookup(map, key, &value);
    check(hr == S_OK && value, "Resources[\"AccentBrush\"], hr %#lx", hr);
    if (value)
    {
        IInspectable_QueryInterface(value, &IID_ISolidColorBrush, (void **)&brush);
        ISolidColorBrush_get_Color(brush, &color);
        check(color.A == 0xff && color.R == 0x00 && color.G == 0x63 && color.B == 0xb1, "AccentBrush is #FF0063B1");
        ISolidColorBrush_Release(brush);
        IInspectable_Release(value);
    }
    IInspectable_Release(key);
    IPropertyValueStatics_Release(pv);
    IMap_IInspectable_IInspectable_Release(map);
    IResourceDictionary_Release(dict);

    /* MainPage.xaml */
    check(the_page != NULL, "MainPage was created");
    if (!the_page) goto done;
    check(the_page->title != NULL, "Connect(1) gave us x:Name=\"Title\"");
    check(the_page->click_button != NULL, "Connect(2) gave us x:Name=\"ClickButton\"");
    if (!the_page->title || !the_page->click_button) goto done;
    check(!wcscmp(text_of(the_page->title, buf, ARRAYSIZE(buf)), L"Hello from markup"), "Title.Text from markup (%ls)", buf);

    check_styles(the_page);

    snapshot();
    printf("client %dx%d\n", width, height);
    tx = 20; ty = 20;
    bw = actual(the_page->click_button, FALSE);
    bh = actual(the_page->click_button, TRUE);
    check(actual(the_page->title, TRUE) > 30, "Title is 28 px text (%.1f high)", actual(the_page->title, TRUE));
    check(bw > 40 && bh > 25, "ClickButton was laid out (%.1fx%.1f)", bw, bh);
    check(count_color(0x0063b1) > 50000, "the Border's {StaticResource AccentBrush} background is drawn (%d px)", count_color(0x0063b1));
    check(count_color(0xffffff) > 100000, "the page background {ThemeResource ApplicationPageBackgroundThemeBrush} is white (%d px)",
          count_color(0xffffff));
    check(count_color(0xddeeff) > 20000, "the StackPanel's {ThemeResource PanelBrush} comes from the Light theme dictionary (%d px)",
          count_color(0xddeeff));
    check(count_color(0xc50f1f) > 2000, "the styled button is drawn in StyleAccentBrush (%d px)", count_color(0xc50f1f));

    /* click the button: Click="OnClick" was connected by Connect(2) */
    {
        double cx = tx + bw / 2, cy = ty + actual(the_page->title, TRUE) + 10 + bh / 2;
        SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM((int)cx, (int)cy));
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((int)cx, (int)cy));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM((int)cx, (int)cy));
    }
    check(the_page->clicks == 1, "clicking ClickButton ran OnClick (%d)", the_page->clicks);
    check_template(the_page);
    check(!wcscmp(text_of(the_page->title, buf, ARRAYSIZE(buf)), L"Clicked!"), "OnClick changed Title (%ls)", buf);
    snapshot();

    /* Microsoft's compiler output */
    check_microsoft_xbf(L"CustomTypesPage", L"Windows.UI.Xaml.Controls.Page");
    check_microsoft_xbf(L"SampleXbf2", L"Windows.UI.Xaml.Controls.Page");
    check_microsoft_xbf(L"PageWithConditionalXaml", L"Windows.UI.Xaml.Controls.Page");
    check_microsoft_xbf(L"SampleWithChecksum", L"Windows.UI.Xaml.Controls.Page");
    check_microsoft_xbf(L"XbfVsmTest.v2.1", L"Windows.UI.Xaml.Controls.Page");
    check_microsoft_xbf(L"XbfResourceDictionaryTest.v2.1", L"Windows.UI.Xaml.ResourceDictionary");
    check_microsoft_xbf(L"XbfStyleTest.v2.1", L"Windows.UI.Xaml.ResourceDictionary");

done:
    printf("exiting\n");
    IApplication_Exit(app);
    IApplication_Release(app);
    IApplicationStatics_Release(app_statics);
}

/* ---- Application.Start callback: "ref new App()" ---- */

static HRESULT WINAPI init_QueryInterface(IApplicationInitializationCallback *iface, REFIID iid, void **out) { *out = iface; return S_OK; }
static ULONG WINAPI init_AddRef(IApplicationInitializationCallback *iface) { return 2; }
static ULONG WINAPI init_Release(IApplicationInitializationCallback *iface) { return 1; }

static HRESULT WINAPI init_Invoke(IApplicationInitializationCallback *iface, IApplicationInitializationCallbackParams *params)
{
    IApplicationFactory *factory = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationFactory);
    struct app *app = calloc(1, sizeof(*app));
    IApplication *base;
    HRESULT hr;

    app->IApplicationOverrides_iface.lpVtbl = &app_vtbl;
    app->IXamlMetadataProvider_iface.lpVtbl = &provider_vtbl;
    app->ref = 1;
    IApplicationFactory_CreateInstance(factory, (IInspectable *)&app->IApplicationOverrides_iface, &app->inner, &base);
    IApplicationFactory_Release(factory);
    /* App::InitializeComponent() */
    hr = load_component((IInspectable *)&app->IApplicationOverrides_iface, L"ms-appx:///App.xaml");
    check(hr == S_OK, "LoadComponent(App.xaml), hr %#lx", hr);
    IApplication_Release(base);
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
    hr = IApplicationStatics_Start(statics, &init_callback);
    check(hr == S_OK, "Application.Start returned after exit, hr %#lx", hr);
    IApplicationStatics_Release(statics);
    printf("%d checks, %d failures: %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
