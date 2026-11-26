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
#include "winreg.h"

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
#define WIDL_using_Windows_UI_Xaml_Data
#define WIDL_using_Windows_UI_Xaml_Input
#define WIDL_using_Windows_UI_Xaml_Media_Animation
#define WIDL_using_Windows_UI_Xaml_Controls_Primitives
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
    IToggleButton *check, *three_state, *radio_a, *radio_b, *toggle;   /* milestone 4 part 3 */
    IButton *plain;
    IPanel *toggle_panel;       /* milestone 4 part 4 */
    int checked, unchecked;     /* Checked="OnChecked" Unchecked="OnUnchecked" */
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

/* "OnChecked" and "OnUnchecked": count into the page */
struct counter_handler
{
    IRoutedEventHandler IRoutedEventHandler_iface;
    int *count;
};
static HRESULT WINAPI counter_Invoke(IRoutedEventHandler *iface, IInspectable *sender, IRoutedEventArgs *args)
{
    struct counter_handler *handler = CONTAINING_RECORD(iface, struct counter_handler, IRoutedEventHandler_iface);
    if (handler->count) (*handler->count)++;
    return S_OK;
}
static IRoutedEventHandlerVtbl counter_vtbl = { click_QueryInterface, click_AddRef, click_Release, counter_Invoke };
static struct counter_handler on_checked = { { &counter_vtbl } }, on_unchecked = { { &counter_vtbl } };

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
    case 5: IInspectable_QueryInterface(target, &IID_IPanel, (void **)&page->toggle_panel); break;
    case 6:
        IInspectable_QueryInterface(target, &IID_IToggleButton, (void **)&page->check);
        on_checked.count = &page->checked;
        on_unchecked.count = &page->unchecked;
        if (page->check)
        {
            IToggleButton_add_Checked(page->check, &on_checked.IRoutedEventHandler_iface, &token);
            IToggleButton_add_Unchecked(page->check, &on_unchecked.IRoutedEventHandler_iface, &token);
        }
        break;
    case 7: IInspectable_QueryInterface(target, &IID_IToggleButton, (void **)&page->three_state); break;
    case 8: IInspectable_QueryInterface(target, &IID_IToggleButton, (void **)&page->radio_a); break;
    case 9: IInspectable_QueryInterface(target, &IID_IToggleButton, (void **)&page->radio_b); break;
    case 10: IInspectable_QueryInterface(target, &IID_IToggleButton, (void **)&page->toggle); break;
    case 11: IInspectable_QueryInterface(target, &IID_IButton, (void **)&page->plain); break;
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

/* milestone 4 part 3: ToggleButton, CheckBox, RadioButton, live TemplateBinding and WinUI resource keys */

static int is_checked(IToggleButton *toggle)
{
    IReference_boolean *ref = NULL;
    boolean value;

    if (FAILED(IToggleButton_get_IsChecked(toggle, &ref))) return -2;
    if (!ref) return -1;
    IReference_boolean_get_Value(ref, &value);
    IReference_boolean_Release(ref);
    return value;
}

static BOOL center_of(DWORD rgb, POINT *pt)
{
    RECT box;
    if (find_color(rgb, &box) < 50) return FALSE;
    pt->x = (box.left + box.right) / 2;
    pt->y = (box.top + box.bottom) / 2;
    return TRUE;
}

static void hover(POINT pt)
{
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
}

static void click_at(POINT pt)
{
    hover(pt);
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
}

static void leave(void)
{
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
}

static IBrush *make_brush(UINT32 rgb)
{
    IActivationFactory *factory = get_factory(L"Windows.UI.Xaml.Media.SolidColorBrush", &IID_IActivationFactory);
    ISolidColorBrush *solid;
    IInspectable *object;
    IBrush *brush = NULL;
    Color c = { 0xff, rgb >> 16, (rgb >> 8) & 0xff, rgb & 0xff };

    if (!factory) return NULL;
    IActivationFactory_ActivateInstance(factory, &object);
    IActivationFactory_Release(factory);
    IInspectable_QueryInterface(object, &IID_ISolidColorBrush, (void **)&solid);
    ISolidColorBrush_put_Color(solid, c);
    ISolidColorBrush_QueryInterface(solid, &IID_IBrush, (void **)&brush);
    ISolidColorBrush_Release(solid);
    IInspectable_Release(object);
    return brush;
}

static void set_background(void *object, UINT32 rgb)
{
    IControl *control;
    IBrush *brush = make_brush(rgb);

    if (SUCCEEDED(IUnknown_QueryInterface((IUnknown *)object, &IID_IControl, (void **)&control)))
    {
        IControl_put_Background(control, brush);
        IControl_Release(control);
    }
    if (brush) IBrush_Release(brush);
}

static void check_toggles(struct main_page *page)
{
    POINT check_pt, three_pt, a_pt, b_pt, toggle_pt, plain_pt;
    IReference_boolean *ref;
    IPropertyValueStatics *pv;
    IInspectable *boxed;
    int n1, n2;
    BOOL found;

    check(page->check && page->three_state && page->radio_a && page->radio_b && page->toggle && page->plain,
          "Connect(6-11) gave us the CheckBoxes, RadioButtons, ToggleButton and the plain Button");
    if (!page->check || !page->three_state || !page->radio_a || !page->radio_b || !page->toggle || !page->plain) return;
    leave();
    snapshot();
    found = center_of(0x102030, &check_pt) && center_of(0x203040, &three_pt) && center_of(0x304050, &a_pt) &&
            center_of(0x405060, &b_pt) && center_of(0xffb900, &toggle_pt) && center_of(0x506070, &plain_pt);
    check(found, "each toggle control draws its Background (the ToggleButton through its template)");
    if (!found) return;

    /* the values from markup */
    check(is_checked(page->check) == 0, "Check starts unchecked (%d)", is_checked(page->check));
    check(is_checked(page->three_state) == 1, "ThreeState has IsChecked=\"True\" from markup (%d)", is_checked(page->three_state));
    check(is_checked(page->radio_a) == 1 && is_checked(page->radio_b) == 0, "RadioA has IsChecked=\"True\", RadioB doesn't");

    /* CheckBox: Checked/Unchecked events, and the app's CheckBoxCheckBackgroundFillChecked brush */
    n1 = count_color(0x8e44ad);
    check(n1 > 200, "the checked ThreeState box is filled with the app's CheckBoxCheckBackgroundFillChecked (%d px)", n1);
    click_at(check_pt);
    leave();
    check(is_checked(page->check) == 1 && page->checked == 1, "clicking Check checks it and raises Checked (%d, %d)",
          is_checked(page->check), page->checked);
    snapshot();
    n2 = count_color(0x8e44ad);
    check(n2 > n1 + 200, "the checked box is filled too (%d px, was %d)", n2, n1);
    click_at(check_pt);
    leave();
    snapshot();
    check(is_checked(page->check) == 0 && page->unchecked == 1 && count_color(0x8e44ad) == n1,
          "clicking again unchecks it and raises Unchecked (%d, %d)", is_checked(page->check), page->unchecked);

    /* IsThreeState: checked -> indeterminate (null) -> unchecked */
    click_at(three_pt);
    check(is_checked(page->three_state) == -1, "IsThreeState: Checked goes to Indeterminate, IsChecked null (%d)", is_checked(page->three_state));
    click_at(three_pt);
    leave();
    check(is_checked(page->three_state) == 0, "then to Unchecked (%d)", is_checked(page->three_state));

    /* RadioButtons with the same parent form a group */
    click_at(b_pt);
    leave();
    check(is_checked(page->radio_b) == 1 && is_checked(page->radio_a) == 0, "checking RadioB unchecks RadioA (%d, %d)",
          is_checked(page->radio_b), is_checked(page->radio_a));
    click_at(b_pt);
    leave();
    check(is_checked(page->radio_b) == 1, "clicking a checked RadioButton leaves it checked");
    pv = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
    IPropertyValueStatics_CreateBoolean(pv, TRUE, &boxed);
    IInspectable_QueryInterface(boxed, &IID_IReference_boolean, (void **)&ref);
    IToggleButton_put_IsChecked(page->radio_a, ref);
    IReference_boolean_Release(ref);
    IInspectable_Release(boxed);
    IPropertyValueStatics_Release(pv);
    check(is_checked(page->radio_a) == 1 && is_checked(page->radio_b) == 0, "IsChecked = true from code unchecks the other one too");

    /* the templated ToggleButton: Normal, PointerOver, CheckedPointerOver, Checked */
    hover(toggle_pt);
    snapshot();
    check(count_color(0x5c2d91) > 1000, "ToggleButton template: PointerOver (%d px)", count_color(0x5c2d91));
    click_at(toggle_pt);
    snapshot();
    check(is_checked(page->toggle) == 1 && count_color(0xa80000) > 1000, "clicked: checked, in CheckedPointerOver (%d px)",
          count_color(0xa80000));
    leave();
    snapshot();
    check(count_color(0xe81123) > 1000 && count_color(0xa80000) == 0, "pointer gone: Checked (%d px)", count_color(0xe81123));

    /* live TemplateBinding: a new Background while the Checked state's Setter holds the Grid's background... */
    set_background(page->toggle, 0x00b294);
    snapshot();
    check(count_color(0xe81123) > 1000 && count_color(0x00b294) == 0, "a Background set in the Checked state stays hidden by the state");
    /* ...is what leaving the state restores */
    click_at(toggle_pt);
    leave();
    snapshot();
    check(is_checked(page->toggle) == 0 && count_color(0x00b294) > 1000 && count_color(0xffb900) == 0,
          "unchecked, Normal: the template shows the new Background (%d px)", count_color(0x00b294));

    /* live TemplateBinding in the Normal state: straight through */
    set_background(page->templated, 0xff8c00);
    snapshot();
    check(count_color(0xff8c00) > 1500 && count_color(0x107c10) == 0, "TemplatedButton.Background = #FF8C00 reaches its template (%d px)",
          count_color(0xff8c00));

    /* a built-in Button look takes ButtonBackgroundPointerOver from the app's resources */
    hover(plain_pt);
    snapshot();
    check(count_color(0xf7630c) > 500, "the plain Button's hover colour is the app's ButtonBackgroundPointerOver (%d px)",
          count_color(0xf7630c));
    leave();
}

/* milestone 4 part 4: XAML text (XamlReader.Load), generic.xaml's text styles and default templates */

static const WCHAR loaded_xaml[] =
    L"<StackPanel xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"\n"
    L"            xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\" Margin=\"0,8,0,0\">\n"
    L"  <!-- a comment, and an entity: &amp; -->\n"
    L"  <StackPanel.Resources>\n"
    L"    <SolidColorBrush x:Key=\"TextBrush\" Color=\"#FF00CC6A\"/>\n"
    L"    <Style x:Key=\"LoadedButton\" TargetType=\"Button\">\n"
    L"      <Setter Property=\"Background\" Value=\"{StaticResource TextBrush}\"/>\n"
    L"      <Setter Property=\"Template\">\n"
    L"        <Setter.Value>\n"
    L"          <ControlTemplate TargetType=\"Button\">\n"
    L"            <Border x:Name=\"Root\" Background=\"{TemplateBinding Background}\" Padding=\"12,6,12,6\">\n"
    L"              <VisualStateManager.VisualStateGroups>\n"
    L"                <VisualStateGroup x:Name=\"CommonStates\">\n"
    L"                  <VisualState x:Name=\"Normal\"/>\n"
    L"                  <VisualState x:Name=\"PointerOver\">\n"
    L"                    <VisualState.Setters>\n"
    L"                      <Setter Target=\"Root.Background\" Value=\"#FF8764B8\"/>\n"
    L"                    </VisualState.Setters>\n"
    L"                  </VisualState>\n"
    L"                </VisualStateGroup>\n"
    L"              </VisualStateManager.VisualStateGroups>\n"
    L"              <ContentPresenter Content=\"{TemplateBinding Content}\"/>\n"
    L"            </Border>\n"
    L"          </ControlTemplate>\n"
    L"        </Setter.Value>\n"
    L"      </Setter>\n"
    L"    </Style>\n"
    L"  </StackPanel.Resources>\n"
    L"  <TextBlock x:Name=\"Heading\" Text=\"Text &amp; markup\" Style=\"{StaticResource TitleTextBlockStyle}\"/>\n"
    L"  <Button Content=\"Text template\" Style=\"{StaticResource LoadedButton}\"/>\n"
    L"</StackPanel>\n";

static IInspectable *xaml_load(const WCHAR *text, HRESULT *hr)
{
    IXamlReaderStatics *reader = get_factory(L"Windows.UI.Xaml.Markup.XamlReader", &IID_IXamlReaderStatics);
    IInspectable *object = NULL;
    HSTRING str;

    *hr = E_FAIL;
    if (!reader) return NULL;
    str = hs(text);
    *hr = IXamlReaderStatics_Load(reader, str, &object);
    WindowsDeleteString(str);
    IXamlReaderStatics_Release(reader);
    return object;
}

static void check_text_markup(struct main_page *page)
{
    IVector_UIElement *children;
    IInspectable *object, *item;
    IUIElement *ui;
    ITextBlock *heading = NULL;
    IControl *control;
    IBrush *brush = NULL;
    FontWeight weight;
    UINT32 size = 0, argb = 0;
    POINT pt;
    DOUBLE d;
    HRESULT hr;
    WCHAR buf[64];

    object = xaml_load(L"<StackPanel xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"><Button></StackPanel>", &hr);
    check(FAILED(hr) && !object, "XamlReader.Load refuses malformed XML, hr %#lx", hr);

    object = xaml_load(loaded_xaml, &hr);
    check(hr == S_OK && object, "XamlReader.Load of a StackPanel with resources, a Style and a ControlTemplate, hr %#lx", hr);
    if (!object) return;
    IInspectable_QueryInterface(object, &IID_IPanel, (void **)&ui);
    check(ui != NULL, "the root is a Panel");
    if (ui)
    {
        IPanel_get_Children((IPanel *)ui, &children);
        IVector_UIElement_get_Size(children, &size);
        check(size == 2, "it has the 2 children from the markup (%u)", size);
        if (size == 2)
        {
            IVector_UIElement_GetAt(children, 0, (IUIElement **)&item);
            IInspectable_QueryInterface(item, &IID_ITextBlock, (void **)&heading);
            IInspectable_Release(item);
        }
        IVector_UIElement_Release(children);
        IPanel_Release((IPanel *)ui);
    }
    check(heading != NULL, "the first child is a TextBlock");
    if (heading)
    {
        check(!wcscmp(text_of(heading, buf, ARRAYSIZE(buf)), L"Text & markup"), "its Text has the entity decoded (%ls)", buf);
        ITextBlock_get_FontSize(heading, &d);
        ITextBlock_get_FontWeight(heading, &weight);
        check(d == 24 && weight.Weight == 350, "generic.xaml's TitleTextBlockStyle (BasedOn BaseTextBlockStyle) gives 24 px SemiLight (%.0f, %u)",
              d, weight.Weight);
        ITextBlock_Release(heading);
    }

    /* show it, and its text template */
    if (page->toggle_panel)
    {
        IPanel_get_Children(page->toggle_panel, &children);
        IInspectable_QueryInterface(object, &IID_IUIElement, (void **)&ui);
        IVector_UIElement_Append(children, ui);
        IUIElement_Release(ui);
        IVector_UIElement_Release(children);
        snapshot();
        check(center_of(0x00cc6a, &pt), "the text ControlTemplate draws the style's {StaticResource TextBrush} (%d px)", count_color(0x00cc6a));
        hover(pt);
        snapshot();
        check(count_color(0x8764b8) > 300 && count_color(0x00cc6a) == 0, "and its PointerOver state (%d px)", count_color(0x8764b8));
        leave();
    }
    IInspectable_Release(object);

    /* a plain Button gets generic.xaml's default style and template */
    IButton_QueryInterface(page->plain, &IID_IControl, (void **)&control);
    {
        IControlTemplate *template = NULL;
        IControl_get_Template(control, (void *)&template);
        check(template != NULL, "the plain Button has the default ControlTemplate from generic.xaml");
        if (template) IUnknown_Release((IUnknown *)template);
    }
    IControl_get_Background(control, &brush);
    check(brush_color(brush, &argb) && argb == 0x33000000, "its Background is the default style's {ThemeResource ButtonBackground} (%#x)", argb);
    if (brush) IBrush_Release(brush);
    IControl_Release(control);
}

/* milestone 4 part 5: shapes and the CheckBox/RadioButton templates, CornerRadius, theme changes while running,
 * loose .xaml files */

static IControlTemplate *template_of(void *control_object)
{
    IControlTemplate *template = NULL;
    IControl *control;

    if (FAILED(IUnknown_QueryInterface((IUnknown *)control_object, &IID_IControl, (void **)&control))) return NULL;
    IControl_get_Template(control, (void *)&template);
    IControl_Release(control);
    return template;
}

static void check_glyph_templates(struct main_page *page)
{
    IControlTemplate *template;
    RECT box;
    int x, y, white = 0;

    template = template_of(page->check);
    check(template != NULL, "CheckBox has generic.xaml's default template");
    if (template) IUnknown_Release((IUnknown *)template);
    template = template_of(page->radio_a);
    check(template != NULL, "RadioButton has generic.xaml's default template");
    if (template) IUnknown_Release((IUnknown *)template);

    /* the checked box: the template's Rectangle filled with the app's CheckBoxCheckBackgroundFillChecked, with the
     * Path check mark in CheckBoxCheckGlyphForegroundChecked (white) over it */
    IToggleButton_put_IsChecked(page->three_state, NULL);
    {
        IPropertyValueStatics *pv = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
        IReference_boolean *ref;
        IInspectable *boxed;
        IPropertyValueStatics_CreateBoolean(pv, TRUE, &boxed);
        IInspectable_QueryInterface(boxed, &IID_IReference_boolean, (void **)&ref);
        IToggleButton_put_IsChecked(page->three_state, ref);
        IReference_boolean_Release(ref);
        IInspectable_Release(boxed);
        IPropertyValueStatics_Release(pv);
    }
    snapshot();
    if (find_color(0x8e44ad, &box) > 200)
    {
        for (y = box.top; y <= box.bottom; y++) for (x = box.left; x <= box.right; x++)
            if ((pixels[y * width + x] & 0xffffff) == 0xffffff) white++;
        check(box.right - box.left >= 17 && box.right - box.left <= 21, "the template's 20 px box (%ld px)", box.right - box.left + 1);
    }
    check(white > 15, "the template's Path draws the check mark (%d white px in the box)", white);
}

static void check_corners(struct main_page *page)
{
    static const WCHAR xaml[] =
        L"<Border xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" Width=\"100\" Height=\"40\""
        L" HorizontalAlignment=\"Left\" CornerRadius=\"10\" Background=\"#FF2D7D9A\" BorderBrush=\"#FFC239B3\""
        L" BorderThickness=\"3\" Margin=\"0,8,0,0\"/>";
    IVector_UIElement *children;
    IInspectable *object;
    IUIElement *ui;
    RECT box, frame;
    HRESULT hr;
    int fill, ring;

    if (!page->toggle_panel || !(object = xaml_load(xaml, &hr))) return;
    IPanel_get_Children(page->toggle_panel, &children);
    IInspectable_QueryInterface(object, &IID_IUIElement, (void **)&ui);
    IVector_UIElement_Append(children, ui);
    IUIElement_Release(ui);
    IVector_UIElement_Release(children);
    IInspectable_Release(object);
    snapshot();
    fill = find_color(0x2d7d9a, &box);
    ring = find_color(0xc239b3, &frame);
    check(ring > 400 && frame.right - frame.left == 99 && frame.bottom - frame.top == 39, "a Border with CornerRadius 10 draws its border (%d px, %ldx%ld)",
          ring, frame.right - frame.left + 1, frame.bottom - frame.top + 1);
    check((pixels[frame.top * width + frame.left] & 0xffffff) != 0xc239b3 &&
          (pixels[frame.top * width + frame.left + 50] & 0xffffff) == 0xc239b3, "with rounded corners: the corner pixel is outside it, the top edge inside");
    check(fill > 2500 && fill < 94 * 34, "the rounded background fills inside (%d px)", fill);
}

/* the page's root Grid as a FrameworkElement2 */
static IFrameworkElement2 *page_root(struct main_page *page)
{
    IFrameworkElement2 *fe2 = NULL;
    IUserControl *uc;
    IUIElement *root = NULL;

    if (FAILED(IInspectable_QueryInterface(page->inner, &IID_IUserControl, (void **)&uc))) return NULL;
    IUserControl_get_Content(uc, &root);
    IUserControl_Release(uc);
    if (!root) return NULL;
    IUIElement_QueryInterface(root, &IID_IFrameworkElement2, (void **)&fe2);
    IUIElement_Release(root);
    return fe2;
}

static void check_theme_change(struct main_page *page)
{
    IFrameworkElement2 *root = page_root(page);
    IControl *control;
    IBrush *brush = NULL;
    UINT32 argb = 0;
    HKEY key;
    DWORD dark = 0;
    int light_panel;

    check(root != NULL, "the page's root Grid has IFrameworkElement2");
    if (!root) return;
    snapshot();
    light_panel = count_color(0xddeeff);

    IFrameworkElement2_put_RequestedTheme(root, ElementTheme_Dark);
    snapshot();
    check(count_color(0x102030) > 20000 && count_color(0xddeeff) == 0,
          "RequestedTheme=Dark on the root: the StackPanel's {ThemeResource PanelBrush} comes from App.xaml's Dark dictionary (%d px)",
          count_color(0x102030));
    check(count_color(0x000000) > 100000, "and the page's {ThemeResource ApplicationPageBackgroundThemeBrush} is generic.xaml's dark one (%d px)",
          count_color(0x000000));
    IButton_QueryInterface(page->plain, &IID_IControl, (void **)&control);
    IControl_get_Background(control, &brush);
    check(brush_color(brush, &argb) && argb == 0x33ffffff, "the plain Button's default style now gives the dark ButtonBackground (%#x)", argb);
    if (brush) IBrush_Release(brush);
    brush = NULL;

    IFrameworkElement2_put_RequestedTheme(root, ElementTheme_Default);
    snapshot();
    check(count_color(0xddeeff) == light_panel && count_color(0x102030) < 20000, "back to Default: the Light resources return (%d px)",
          count_color(0xddeeff));
    IControl_get_Background(control, &brush);
    check(brush_color(brush, &argb) && argb == 0x33000000, "and the light ButtonBackground (%#x)", argb);
    if (brush) IBrush_Release(brush);
    IControl_Release(control);

    /* the system going dark doesn't change an app whose App.xaml sets RequestedTheme="Light" */
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, NULL, 0,
                         KEY_ALL_ACCESS, NULL, &key, NULL))
    {
        RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD, (BYTE *)&dark, sizeof(dark));
        SendMessageW(hwnd, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
        snapshot();
        check(count_color(0xddeeff) == light_panel, "a system theme change leaves an app with an explicit RequestedTheme alone");
        RegDeleteValueW(key, L"AppsUseLightTheme");
        RegCloseKey(key);
    }
    IFrameworkElement2_Release(root);
}

/* a component for LoosePage.xaml: a Page aggregated by an IComponentConnector that records Connect calls */
struct loose_page
{
    IComponentConnector IComponentConnector_iface;
    IInspectable *inner;
    LONG ref;
    int connects;
    IBorder *border;
};

static struct loose_page *loose_from_connector(IComponentConnector *iface)
{
    return CONTAINING_RECORD(iface, struct loose_page, IComponentConnector_iface);
}

static HRESULT WINAPI loose_QueryInterface(IComponentConnector *iface, REFIID iid, void **out)
{
    struct loose_page *page = loose_from_connector(iface);
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IComponentConnector))
    {
        *out = iface;
        IComponentConnector_AddRef(iface);
        return S_OK;
    }
    return IInspectable_QueryInterface(page->inner, iid, out);
}
static ULONG WINAPI loose_AddRef(IComponentConnector *iface) { return InterlockedIncrement(&loose_from_connector(iface)->ref); }
static ULONG WINAPI loose_Release(IComponentConnector *iface) { return InterlockedDecrement(&loose_from_connector(iface)->ref); }
static HRESULT WINAPI loose_GetIids(IComponentConnector *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI loose_GetRuntimeClassName(IComponentConnector *iface, HSTRING *name) { *name = hs(L"XamlMarkup.LoosePage"); return S_OK; }
static HRESULT WINAPI loose_GetTrustLevel(IComponentConnector *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI loose_Connect(IComponentConnector *iface, INT32 id, IInspectable *target)
{
    struct loose_page *page = loose_from_connector(iface);
    page->connects++;
    if (id == 1 && !page->border) IInspectable_QueryInterface(target, &IID_IBorder, (void **)&page->border);
    return S_OK;
}
static IComponentConnectorVtbl loose_vtbl =
{
    loose_QueryInterface, loose_AddRef, loose_Release, loose_GetIids, loose_GetRuntimeClassName, loose_GetTrustLevel, loose_Connect,
};

static void check_loose_xaml(void)
{
    IPageFactory *factory = get_factory(L"Windows.UI.Xaml.Controls.Page", &IID_IPageFactory);
    struct loose_page page = { { &loose_vtbl }, NULL, 1 };
    IFrameworkElement *fe;
    IInspectable *found = NULL;
    ITextBlock *tb;
    CornerRadius radius = {0};
    HSTRING name;
    IPage *base;
    HRESULT hr;
    WCHAR buf[64];

    if (!factory) return;
    hr = IPageFactory_CreateInstance(factory, (IInspectable *)&page.IComponentConnector_iface, &page.inner, &base);
    IPageFactory_Release(factory);
    if (FAILED(hr)) return;
    IPage_Release(base);
    hr = load_component((IInspectable *)&page.IComponentConnector_iface, L"ms-appx:///LoosePage.xaml");
    check(hr == S_OK, "LoadComponent of a loose LoosePage.xaml (no .xbf), hr %#lx", hr);
    check(page.connects == 1 && page.border, "its x:ConnectionId=\"1\" reached Connect with the Border (%d)", page.connects);
    if (page.border)
    {
        IBorder_get_CornerRadius(page.border, &radius);
        check(radius.TopLeft == 12 && radius.BottomRight == 12, "CornerRadius=\"12\" (%.0f)", radius.TopLeft);
        IBorder_Release(page.border);
    }
    IInspectable_QueryInterface(page.inner, &IID_IFrameworkElement, (void **)&fe);
    name = hs(L"LooseText");
    IFrameworkElement_FindName(fe, name, &found);
    WindowsDeleteString(name);
    check(found != NULL, "FindName(\"LooseText\") finds the TextBlock");
    if (found && SUCCEEDED(IInspectable_QueryInterface(found, &IID_ITextBlock, (void **)&tb)))
    {
        check(!wcscmp(text_of(tb, buf, ARRAYSIZE(buf)), L"Loose XAML"), "with its Text (%ls)", buf);
        ITextBlock_Release(tb);
    }
    if (found) IInspectable_Release(found);
    IFrameworkElement_Release(fe);
    IInspectable_Release(page.inner);
}

/* milestone 4 part 6: Opacity, Path curves and geometries, ToggleSwitch/Slider/ProgressBar, animation timing,
 * DataTemplate/ItemsControl and {Binding} */

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while ((int)(end - GetTickCount()) > 0)
    {
        /* frames can take longer than the animation timer: don't let a full queue run past the end */
        while ((int)(end - GetTickCount()) > 0 && PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
}

static IInspectable *find_in(IInspectable *root, const WCHAR *name)
{
    IFrameworkElement *fe;
    IInspectable *found = NULL;
    HSTRING str;

    if (FAILED(IInspectable_QueryInterface(root, &IID_IFrameworkElement, (void **)&fe))) return NULL;
    str = hs(name);
    IFrameworkElement_FindName(fe, str, &found);
    WindowsDeleteString(str);
    IFrameworkElement_Release(fe);
    return found;
}

static DOUBLE opacity_of(IInspectable *object)
{
    IUIElement *ui;
    DOUBLE d = -1;
    if (!object || FAILED(IInspectable_QueryInterface(object, &IID_IUIElement, (void **)&ui))) return -1;
    IUIElement_get_Opacity(ui, &d);
    IUIElement_Release(ui);
    return d;
}

/* a data object for bindings: Name, Count, Color and Items through ICustomPropertyProvider, and PropertyChanged */
struct data_object
{
    ICustomPropertyProvider ICustomPropertyProvider_iface;
    INotifyPropertyChanged INotifyPropertyChanged_iface;
    LONG ref;
    WCHAR name[32], color[16];
    double count;
    IInspectable *items;
    IPropertyChangedEventHandler *handlers[8];
};

struct data_property
{
    ICustomProperty ICustomProperty_iface;
    LONG ref;
    WCHAR name[32];
};

static IInspectable *box_str(const WCHAR *str)
{
    IPropertyValueStatics *pv = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
    IInspectable *value = NULL;
    HSTRING h = hs(str);
    IPropertyValueStatics_CreateString(pv, h, &value);
    WindowsDeleteString(h);
    IPropertyValueStatics_Release(pv);
    return value;
}

static IInspectable *box_dbl(double d)
{
    IPropertyValueStatics *pv = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
    IInspectable *value = NULL;
    IPropertyValueStatics_CreateDouble(pv, d, &value);
    IPropertyValueStatics_Release(pv);
    return value;
}

static struct data_object *data_from_provider(ICustomPropertyProvider *iface)
{
    return CONTAINING_RECORD(iface, struct data_object, ICustomPropertyProvider_iface);
}

static HRESULT WINAPI data_QueryInterface(ICustomPropertyProvider *iface, REFIID iid, void **out)
{
    struct data_object *obj = data_from_provider(iface);
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_ICustomPropertyProvider))
        *out = &obj->ICustomPropertyProvider_iface;
    else if (IsEqualGUID(iid, &IID_INotifyPropertyChanged)) *out = &obj->INotifyPropertyChanged_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&obj->ref);
    return S_OK;
}
static ULONG WINAPI data_AddRef(ICustomPropertyProvider *iface) { return InterlockedIncrement(&data_from_provider(iface)->ref); }
static ULONG WINAPI data_Release(ICustomPropertyProvider *iface) { return InterlockedDecrement(&data_from_provider(iface)->ref); }
static HRESULT WINAPI data_GetIids(ICustomPropertyProvider *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI data_GetRuntimeClassName(ICustomPropertyProvider *iface, HSTRING *name) { *name = hs(L"XamlMarkup.Data"); return S_OK; }
static HRESULT WINAPI data_GetTrustLevel(ICustomPropertyProvider *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }

static struct data_property *prop_from_iface(ICustomProperty *iface)
{
    return CONTAINING_RECORD(iface, struct data_property, ICustomProperty_iface);
}
static HRESULT WINAPI prop_QueryInterface(ICustomProperty *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_ICustomProperty))
    {
        *out = iface;
        InterlockedIncrement(&prop_from_iface(iface)->ref);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI prop_AddRef(ICustomProperty *iface) { return InterlockedIncrement(&prop_from_iface(iface)->ref); }
static ULONG WINAPI prop_Release(ICustomProperty *iface)
{
    ULONG ref = InterlockedDecrement(&prop_from_iface(iface)->ref);
    if (!ref) free(prop_from_iface(iface));
    return ref;
}
static HRESULT WINAPI prop_GetIids(ICustomProperty *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI prop_GetRuntimeClassName(ICustomProperty *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI prop_GetTrustLevel(ICustomProperty *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI prop_get_Type(ICustomProperty *iface, TypeName *value) { memset(value, 0, sizeof(*value)); return S_OK; }
static HRESULT WINAPI prop_get_Name(ICustomProperty *iface, HSTRING *value) { *value = hs(prop_from_iface(iface)->name); return S_OK; }
static HRESULT WINAPI prop_GetValue(ICustomProperty *iface, IInspectable *target, IInspectable **value)
{
    struct data_object *obj = data_from_provider((ICustomPropertyProvider *)target);
    const WCHAR *name = prop_from_iface(iface)->name;
    *value = NULL;
    if (!wcscmp(name, L"Name")) *value = box_str(obj->name);
    else if (!wcscmp(name, L"Color")) *value = box_str(obj->color);
    else if (!wcscmp(name, L"Count")) *value = box_dbl(obj->count);
    else if (!wcscmp(name, L"Items") && obj->items) { IInspectable_AddRef(obj->items); *value = obj->items; }
    return S_OK;
}
static HRESULT WINAPI prop_SetValue(ICustomProperty *iface, IInspectable *target, IInspectable *value)
{
    struct data_object *obj = data_from_provider((ICustomPropertyProvider *)target);
    IPropertyValue *pv;
    if (wcscmp(prop_from_iface(iface)->name, L"Count") || !value) return E_NOTIMPL;
    if (FAILED(IInspectable_QueryInterface(value, &IID_IPropertyValue, (void **)&pv))) return E_INVALIDARG;
    IPropertyValue_GetDouble(pv, &obj->count);
    IPropertyValue_Release(pv);
    return S_OK;
}
static HRESULT WINAPI prop_GetIndexedValue(ICustomProperty *iface, IInspectable *target, IInspectable *index, IInspectable **value) { return E_NOTIMPL; }
static HRESULT WINAPI prop_SetIndexedValue(ICustomProperty *iface, IInspectable *target, IInspectable *value, IInspectable *index) { return E_NOTIMPL; }
static HRESULT WINAPI prop_get_CanWrite(ICustomProperty *iface, boolean *value) { *value = TRUE; return S_OK; }
static HRESULT WINAPI prop_get_CanRead(ICustomProperty *iface, boolean *value) { *value = TRUE; return S_OK; }
static ICustomPropertyVtbl prop_vtbl =
{
    prop_QueryInterface, prop_AddRef, prop_Release, prop_GetIids, prop_GetRuntimeClassName, prop_GetTrustLevel,
    prop_get_Type, prop_get_Name, prop_GetValue, prop_SetValue, prop_GetIndexedValue, prop_SetIndexedValue,
    prop_get_CanWrite, prop_get_CanRead,
};

static HRESULT WINAPI data_GetCustomProperty(ICustomPropertyProvider *iface, HSTRING name, ICustomProperty **value)
{
    struct data_property *prop = calloc(1, sizeof(*prop));
    prop->ICustomProperty_iface.lpVtbl = &prop_vtbl;
    prop->ref = 1;
    lstrcpynW(prop->name, WindowsGetStringRawBuffer(name, NULL), ARRAYSIZE(prop->name));
    *value = &prop->ICustomProperty_iface;
    return S_OK;
}
static HRESULT WINAPI data_GetIndexedProperty(ICustomPropertyProvider *iface, HSTRING name, TypeName type, ICustomProperty **value) { return E_NOTIMPL; }
static HRESULT WINAPI data_GetStringRepresentation(ICustomPropertyProvider *iface, HSTRING *value) { *value = hs(data_from_provider(iface)->name); return S_OK; }
static HRESULT WINAPI data_get_Type(ICustomPropertyProvider *iface, TypeName *value) { memset(value, 0, sizeof(*value)); return S_OK; }
static ICustomPropertyProviderVtbl data_vtbl =
{
    data_QueryInterface, data_AddRef, data_Release, data_GetIids, data_GetRuntimeClassName, data_GetTrustLevel,
    data_GetCustomProperty, data_GetIndexedProperty, data_GetStringRepresentation, data_get_Type,
};

static struct data_object *data_from_notify(INotifyPropertyChanged *iface)
{
    return CONTAINING_RECORD(iface, struct data_object, INotifyPropertyChanged_iface);
}
static HRESULT WINAPI notify_QueryInterface(INotifyPropertyChanged *iface, REFIID iid, void **out)
{
    return data_QueryInterface(&data_from_notify(iface)->ICustomPropertyProvider_iface, iid, out);
}
static ULONG WINAPI notify_AddRef(INotifyPropertyChanged *iface) { return data_AddRef(&data_from_notify(iface)->ICustomPropertyProvider_iface); }
static ULONG WINAPI notify_Release(INotifyPropertyChanged *iface) { return data_Release(&data_from_notify(iface)->ICustomPropertyProvider_iface); }
static HRESULT WINAPI notify_GetIids(INotifyPropertyChanged *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI notify_GetRuntimeClassName(INotifyPropertyChanged *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI notify_GetTrustLevel(INotifyPropertyChanged *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI notify_add_PropertyChanged(INotifyPropertyChanged *iface, IPropertyChangedEventHandler *handler, EventRegistrationToken *token)
{
    struct data_object *obj = data_from_notify(iface);
    int i;
    for (i = 0; i < 8 && obj->handlers[i]; i++);
    if (i == 8) return E_OUTOFMEMORY;
    obj->handlers[i] = handler;
    token->value = i + 1;
    return S_OK;
}
static HRESULT WINAPI notify_remove_PropertyChanged(INotifyPropertyChanged *iface, EventRegistrationToken token)
{
    if (token.value >= 1 && token.value <= 8) data_from_notify(iface)->handlers[token.value - 1] = NULL;
    return S_OK;
}
static INotifyPropertyChangedVtbl notify_vtbl =
{
    notify_QueryInterface, notify_AddRef, notify_Release, notify_GetIids, notify_GetRuntimeClassName, notify_GetTrustLevel,
    notify_add_PropertyChanged, notify_remove_PropertyChanged,
};

static void data_init(struct data_object *obj, const WCHAR *name, const WCHAR *color, double count)
{
    memset(obj, 0, sizeof(*obj));
    obj->ICustomPropertyProvider_iface.lpVtbl = &data_vtbl;
    obj->INotifyPropertyChanged_iface.lpVtbl = &notify_vtbl;
    obj->ref = 1;
    lstrcpyW(obj->name, name);
    lstrcpyW(obj->color, color);
    obj->count = count;
}

/* "PropertyChanged(name)" from the data object, as an app's setter raises it */
static void data_changed(struct data_object *obj, const WCHAR *property)
{
    IActivationFactory *factory;
    IPropertyChangedEventArgsFactory *args_factory;
    IPropertyChangedEventArgs *args = NULL;
    IInspectable *inner = NULL;
    HSTRING name;
    int i;

    if (!(factory = get_factory(L"Windows.UI.Xaml.Data.PropertyChangedEventArgs", &IID_IActivationFactory))) return;
    if (SUCCEEDED(IActivationFactory_QueryInterface(factory, &IID_IPropertyChangedEventArgsFactory, (void **)&args_factory)))
    {
        name = hs(property);
        IPropertyChangedEventArgsFactory_CreateInstance(args_factory, name, NULL, &inner, &args);
        WindowsDeleteString(name);
        IPropertyChangedEventArgsFactory_Release(args_factory);
    }
    IActivationFactory_Release(factory);
    for (i = 0; i < 8; i++)
        if (obj->handlers[i]) IPropertyChangedEventHandler_Invoke(obj->handlers[i], (IInspectable *)&obj->ICustomPropertyProvider_iface, args);
    if (args) IPropertyChangedEventArgs_Release(args);
}

/* a three-item IIterable<Object> for ItemsSource */
struct item_list
{
    IIterable_IInspectable IIterable_iface;
    IIterator_IInspectable IIterator_iface;
    IInspectable *items[3];
    UINT32 pos;
};
static struct item_list *list_from_iterable(IIterable_IInspectable *iface) { return CONTAINING_RECORD(iface, struct item_list, IIterable_iface); }
static struct item_list *list_from_iterator(IIterator_IInspectable *iface) { return CONTAINING_RECORD(iface, struct item_list, IIterator_iface); }
static HRESULT WINAPI list_QueryInterface(IIterable_IInspectable *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IIterable_IInspectable))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI list_AddRef(IIterable_IInspectable *iface) { return 2; }
static ULONG WINAPI list_Release(IIterable_IInspectable *iface) { return 1; }
static HRESULT WINAPI list_GetIids(IIterable_IInspectable *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI list_GetRuntimeClassName(IIterable_IInspectable *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI list_GetTrustLevel(IIterable_IInspectable *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI list_First(IIterable_IInspectable *iface, IIterator_IInspectable **value)
{
    struct item_list *list = list_from_iterable(iface);
    list->pos = 0;
    *value = &list->IIterator_iface;
    return S_OK;
}
static IIterable_IInspectableVtbl list_vtbl = { list_QueryInterface, list_AddRef, list_Release, list_GetIids, list_GetRuntimeClassName, list_GetTrustLevel, list_First };
static HRESULT WINAPI iter_QueryInterface(IIterator_IInspectable *iface, REFIID iid, void **out) { *out = iface; return S_OK; }
static ULONG WINAPI iter_AddRef(IIterator_IInspectable *iface) { return 2; }
static ULONG WINAPI iter_Release(IIterator_IInspectable *iface) { return 1; }
static HRESULT WINAPI iter_GetIids(IIterator_IInspectable *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI iter_GetRuntimeClassName(IIterator_IInspectable *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI iter_GetTrustLevel(IIterator_IInspectable *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI iter_get_Current(IIterator_IInspectable *iface, IInspectable **value)
{
    struct item_list *list = list_from_iterator(iface);
    if (list->pos >= 3) return E_BOUNDS;
    *value = list->items[list->pos];
    IInspectable_AddRef(*value);
    return S_OK;
}
static HRESULT WINAPI iter_get_HasCurrent(IIterator_IInspectable *iface, boolean *value) { *value = list_from_iterator(iface)->pos < 3; return S_OK; }
static HRESULT WINAPI iter_MoveNext(IIterator_IInspectable *iface, boolean *value)
{
    struct item_list *list = list_from_iterator(iface);
    if (list->pos < 3) list->pos++;
    *value = list->pos < 3;
    return S_OK;
}
static HRESULT WINAPI iter_GetMany(IIterator_IInspectable *iface, UINT32 size, IInspectable **items, UINT32 *count) { return E_NOTIMPL; }
static IIterator_IInspectableVtbl iter_vtbl =
{
    iter_QueryInterface, iter_AddRef, iter_Release, iter_GetIids, iter_GetRuntimeClassName, iter_GetTrustLevel,
    iter_get_Current, iter_get_HasCurrent, iter_MoveNext, iter_GetMany,
};

static int toggled_count, value_changed_count, completed_count;

static HRESULT WINAPI toggled_Invoke(IRoutedEventHandler *iface, IInspectable *sender, IRoutedEventArgs *args) { toggled_count++; return S_OK; }
static IRoutedEventHandlerVtbl toggled_vtbl = { click_QueryInterface, click_AddRef, click_Release, toggled_Invoke };
static IRoutedEventHandler on_toggled = { &toggled_vtbl };

static HRESULT WINAPI value_QueryInterface(IRangeBaseValueChangedEventHandler *iface, REFIID iid, void **out) { *out = iface; return S_OK; }
static ULONG WINAPI value_AddRef(IRangeBaseValueChangedEventHandler *iface) { return 2; }
static ULONG WINAPI value_Release(IRangeBaseValueChangedEventHandler *iface) { return 1; }
static HRESULT WINAPI value_Invoke(IRangeBaseValueChangedEventHandler *iface, IInspectable *sender, IRangeBaseValueChangedEventArgs *args) { value_changed_count++; return S_OK; }
static IRangeBaseValueChangedEventHandlerVtbl value_vtbl = { value_QueryInterface, value_AddRef, value_Release, value_Invoke };
static IRangeBaseValueChangedEventHandler on_value = { &value_vtbl };

static HRESULT WINAPI completed_QueryInterface(IEventHandler_IInspectable *iface, REFIID iid, void **out) { *out = iface; return S_OK; }
static ULONG WINAPI completed_AddRef(IEventHandler_IInspectable *iface) { return 2; }
static ULONG WINAPI completed_Release(IEventHandler_IInspectable *iface) { return 1; }
static HRESULT WINAPI completed_Invoke(IEventHandler_IInspectable *iface, IInspectable *sender, IInspectable *args) { completed_count++; return S_OK; }
static IEventHandler_IInspectableVtbl completed_vtbl = { completed_QueryInterface, completed_AddRef, completed_Release, completed_Invoke };
static IEventHandler_IInspectable on_completed = { &completed_vtbl };

static const WCHAR part6_xaml[] =
    L"<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"\n"
    L"      xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"\n"
    L"      Grid.RowSpan=\"2\" Width=\"400\" HorizontalAlignment=\"Center\" Background=\"White\">\n"
    L"  <Grid.Resources>\n"
    L"    <Storyboard x:Key=\"Fade\">\n"
    L"      <DoubleAnimation Storyboard.TargetName=\"FadeBox\" Storyboard.TargetProperty=\"Opacity\" From=\"1\" To=\"0\" Duration=\"0:0:0.3\"/>\n"
    L"    </Storyboard>\n"
    L"    <Style x:Key=\"SlowButton\" TargetType=\"Button\">\n"
    L"      <Setter Property=\"Template\"><Setter.Value>\n"
    L"        <ControlTemplate TargetType=\"Button\">\n"
    L"          <Border x:Name=\"Root\" Background=\"#FF000080\" Width=\"80\" Height=\"30\">\n"
    L"            <VisualStateManager.VisualStateGroups>\n"
    L"              <VisualStateGroup x:Name=\"CommonStates\">\n"
    L"                <VisualState x:Name=\"Normal\"/>\n"
    L"                <VisualState x:Name=\"PointerOver\">\n"
    L"                  <Storyboard>\n"
    L"                    <ColorAnimation Storyboard.TargetName=\"Root\" Storyboard.TargetProperty=\"(Border.Background).(SolidColorBrush.Color)\"\n"
    L"                                    To=\"#FF00FF80\" Duration=\"0:0:0.4\"/>\n"
    L"                  </Storyboard>\n"
    L"                </VisualState>\n"
    L"              </VisualStateGroup>\n"
    L"            </VisualStateManager.VisualStateGroups>\n"
    L"          </Border>\n"
    L"        </ControlTemplate>\n"
    L"      </Setter.Value></Setter>\n"
    L"    </Style>\n"
    L"  </Grid.Resources>\n"
    L"  <StackPanel>\n"
    L"    <Border x:Name=\"Half\" Background=\"#FFFF0000\" Opacity=\"0.5\" Height=\"20\"/>\n"
    L"    <Border x:Name=\"FadeBox\" Background=\"#FF00A0A0\" Height=\"20\"/>\n"
    L"    <Path Stroke=\"#FFA000A0\" StrokeThickness=\"3\" Data=\"M 10,30 C 60,-10 110,70 160,30 Q 200,0 240,30 A 20,20 0 0 1 280,30\"/>\n"
    L"    <Path Fill=\"#FF308030\"><Path.Data><EllipseGeometry Center=\"40,20\" RadiusX=\"40\" RadiusY=\"20\"/></Path.Data></Path>\n"
    L"    <ToggleSwitch x:Name=\"Switch\" Header=\"Wi-Fi\"/>\n"
    L"    <Slider x:Name=\"Slide\" Minimum=\"0\" Maximum=\"100\" Value=\"25\" StepFrequency=\"5\" Width=\"200\" HorizontalAlignment=\"Left\"/>\n"
    L"    <ProgressBar x:Name=\"Progress\" Value=\"50\" Width=\"200\" HorizontalAlignment=\"Left\" Foreground=\"#FFC04000\"/>\n"
    L"    <Button x:Name=\"Slow\" Style=\"{StaticResource SlowButton}\"/>\n"
    L"    <TextBlock x:Name=\"Bound\" Text=\"{Binding Name}\"/>\n"
    L"    <ItemsControl x:Name=\"List\" ItemsSource=\"{Binding Items}\">\n"
    L"      <ItemsControl.ItemTemplate>\n"
    L"        <DataTemplate><Border Height=\"12\" Background=\"{Binding Color}\"/></DataTemplate>\n"
    L"      </ItemsControl.ItemTemplate>\n"
    L"    </ItemsControl>\n"
    L"    <Slider x:Name=\"TwoWay\" Maximum=\"100\" Value=\"{Binding Count, Mode=TwoWay}\" Width=\"200\" HorizontalAlignment=\"Left\"/>\n"
    L"    <TextBlock x:Name=\"Echo\" Text=\"{Binding Value, ElementName=TwoWay}\"/>\n"
    L"    <Button x:Name=\"Templated\" Content=\"{Binding}\">\n"
    L"      <Button.ContentTemplate><DataTemplate><Border Width=\"200\" Height=\"10\" Background=\"{Binding Color}\"/></DataTemplate></Button.ContentTemplate>\n"
    L"    </Button>\n"
    L"  </StackPanel>\n"
    L"</Grid>\n";

static void check_part6(struct main_page *page)
{
    struct data_object data, a, b, c;
    struct item_list list = { { &list_vtbl }, { &iter_vtbl } };
    IInspectable *root, *object, *fade;
    IFrameworkElement2 *page_grid = page_root(page);
    IPanel *grid_panel;
    IVector_UIElement *children;
    IUIElement *ui;
    IFrameworkElement *fe;
    IToggleSwitch *ts;
    IRangeBase *range;
    IResourceDictionary *dict;
    IMap_IInspectable_IInspectable *map;
    IStoryboard *storyboard;
    ITimeline *timeline;
    EventRegistrationToken token;
    ITextBlock *tb;
    RECT box;
    POINT pt;
    DOUBLE d = 0;
    boolean on = FALSE;
    HRESULT hr;
    WCHAR buf[64];
    int n, before;

    data_init(&a, L"One", L"#FF102080", 0);
    data_init(&b, L"Two", L"#FF208010", 0);
    data_init(&c, L"Three", L"#FF801020", 0);
    list.items[0] = (IInspectable *)&a.ICustomPropertyProvider_iface;
    list.items[1] = (IInspectable *)&b.ICustomPropertyProvider_iface;
    list.items[2] = (IInspectable *)&c.ICustomPropertyProvider_iface;
    data_init(&data, L"Alpha", L"#FF5050F0", 40);
    data.items = (IInspectable *)&list.IIterable_iface;

    root = xaml_load(part6_xaml, &hr);
    check(hr == S_OK && root, "XamlReader.Load of the part 6 page, hr %#lx", hr);
    if (!root || !page_grid) return;
    IInspectable_QueryInterface(root, &IID_IFrameworkElement, (void **)&fe);
    IFrameworkElement_put_DataContext(fe, (IInspectable *)&data.ICustomPropertyProvider_iface);
    IFrameworkElement2_QueryInterface(page_grid, &IID_IPanel, (void **)&grid_panel);
    IPanel_get_Children(grid_panel, &children);
    IInspectable_QueryInterface(root, &IID_IUIElement, (void **)&ui);
    IVector_UIElement_Append(children, ui);
    snapshot();

    /* Opacity: #FF0000 at 0.5 over white */
    n = count_color(0xff8080) + count_color(0xff7f7f);
    check(n > 2000, "a Border with Opacity 0.5 draws its red half-blended over white (%d px)", n);
    /* curves and geometries */
    n = find_color(0xa000a0, &box);
    check(n > 300 && box.bottom - box.top > 12, "a Path with C, Q and A segments draws a curve (%d px, %ld high)", n, box.bottom - box.top + 1);
    n = count_color(0x308030);
    check(n > 2200 && n < 2800, "a Path with an EllipseGeometry fills about pi*40*20 px (%d)", n);
    /* ProgressBar at 50 of 100, 200 wide, 4 high */
    n = count_color(0xc04000);
    check(n >= 380 && n <= 420, "ProgressBar Value 50 fills half its width (%d px)", n);

    /* ToggleSwitch */
    object = find_in(root, L"Switch");
    check(object && SUCCEEDED(IInspectable_QueryInterface(object, &IID_IToggleSwitch, (void **)&ts)), "FindName finds the ToggleSwitch");
    if (object && ts)
    {
        IToggleSwitch_add_Toggled(ts, &on_toggled, &token);
        before = count_color(0x0078d7);
        IToggleSwitch_put_IsOn(ts, TRUE);
        snapshot();
        IToggleSwitch_get_IsOn(ts, &on);
        check(on && toggled_count == 1, "IsOn = true raises Toggled (%d)", toggled_count);
        check(count_color(0x0078d7) > before + 400, "the On track is drawn in the accent colour (%d px, was %d)", count_color(0x0078d7), before);
        IToggleSwitch_Release(ts);
    }
    if (object) IInspectable_Release(object);

    /* Slider: a click at three quarters of its track */
    object = find_in(root, L"Slide");
    if (object && SUCCEEDED(IInspectable_QueryInterface(object, &IID_IRangeBase, (void **)&range)))
    {
        IRangeBase_add_ValueChanged(range, &on_value, &token);
        IRangeBase_get_Value(range, &d);
        check(d == 25, "Slider Value from markup (%.1f)", d);
        IRangeBase_Release(range);
    }
    if (object) IInspectable_Release(object);

    /* Storyboard.Begin from code, with timing and Completed */
    IInspectable_QueryInterface(root, &IID_IFrameworkElement, (void **)&fe);
    IFrameworkElement_get_Resources(fe, &dict);
    IResourceDictionary_QueryInterface(dict, &IID_IMap_IInspectable_IInspectable, (void **)&map);
    {
        IInspectable *key = box_str(L"Fade");
        fade = NULL;
        IMap_IInspectable_IInspectable_Lookup(map, key, &fade);
        IInspectable_Release(key);
    }
    check(fade && SUCCEEDED(IInspectable_QueryInterface(fade, &IID_IStoryboard, (void **)&storyboard)), "Resources[\"Fade\"] is a Storyboard");
    if (fade && storyboard)
    {
        object = find_in(root, L"FadeBox");
        IStoryboard_QueryInterface(storyboard, &IID_ITimeline, (void **)&timeline);
        ITimeline_add_Completed(timeline, &on_completed, &token);
        IStoryboard_Begin(storyboard);
        d = opacity_of(object);
        check(d > 0.8, "Begin: the DoubleAnimation starts From 1 (%.2f)", d);
        pump(120);
        d = opacity_of(object);
        check(d > 0.05 && d < 0.95, "after ~120 ms of 300 it is part way (%.2f)", d);
        pump(400);
        d = opacity_of(object);
        check(d == 0 && completed_count == 1, "after 300 ms it holds To 0 and Completed fired (%.2f, %d)", d, completed_count);
        ITimeline_Release(timeline);
        IStoryboard_Release(storyboard);
        if (object) IInspectable_Release(object);
    }
    if (fade) IInspectable_Release(fade);
    IMap_IInspectable_IInspectable_Release(map);
    IResourceDictionary_Release(dict);

    /* a timed visual state: ColorAnimation over 400 ms on PointerOver */
    snapshot();
    if (center_of(0x000080, &pt))
    {
        hover(pt);
        pump(150);
        snapshot();
        check(count_color(0x00ff80) == 0 && count_color(0x000080) == 0, "PointerOver's ColorAnimation is between its colours after 150 ms");
        pump(450);
        snapshot();
        check(count_color(0x00ff80) > 1500, "and at To #00FF80 after 400 ms (%d px)", count_color(0x00ff80));
        leave();
        snapshot();
        check(count_color(0x000080) > 1500, "leaving the state puts the Normal colour back (%d px)", count_color(0x000080));
    }
    else check(0, "the timed template draws #000080");

    /* {Binding}: DataContext, INotifyPropertyChanged, ItemsControl with an ItemTemplate, TwoWay, ElementName, ContentTemplate */
    object = find_in(root, L"Bound");
    if (object && SUCCEEDED(IInspectable_QueryInterface(object, &IID_ITextBlock, (void **)&tb)))
    {
        check(!wcscmp(text_of(tb, buf, ARRAYSIZE(buf)), L"Alpha"), "{Binding Name} from the DataContext (%ls)", buf);
        lstrcpyW(data.name, L"Beta");
        data_changed(&data, L"Name");
        snapshot();
        check(!wcscmp(text_of(tb, buf, ARRAYSIZE(buf)), L"Beta"), "PropertyChanged(\"Name\") updates it (%ls)", buf);
        ITextBlock_Release(tb);
    }
    else check(0, "FindName finds Bound");
    if (object) IInspectable_Release(object);
    check(count_color(0x102080) > 1500 && count_color(0x208010) > 1500 && count_color(0x801020) > 1500,
          "ItemsControl shows its 3 ItemsSource items through the DataTemplate, each Border's {Binding Color} (%d, %d, %d px)",
          count_color(0x102080), count_color(0x208010), count_color(0x801020));
    check(count_color(0x5050f0) > 1500, "Button.ContentTemplate shows its Content {Binding} with the item as DataContext (%d px)",
          count_color(0x5050f0));
    object = find_in(root, L"TwoWay");
    if (object && SUCCEEDED(IInspectable_QueryInterface(object, &IID_IRangeBase, (void **)&range)))
    {
        IRangeBase_get_Value(range, &d);
        check(d == 40, "Slider.Value {Binding Count, Mode=TwoWay} reads 40 (%.1f)", d);
        IRangeBase_put_Value(range, 70);
        check(data.count == 70, "and setting the Slider writes 70 back to the data object (%.1f)", data.count);
        IRangeBase_Release(range);
    }
    else check(0, "FindName finds TwoWay");
    if (object) IInspectable_Release(object);
    snapshot();
    object = find_in(root, L"Echo");
    if (object && SUCCEEDED(IInspectable_QueryInterface(object, &IID_ITextBlock, (void **)&tb)))
    {
        check(!wcscmp(text_of(tb, buf, ARRAYSIZE(buf)), L"70"), "{Binding Value, ElementName=TwoWay} follows the Slider (%ls)", buf);
        ITextBlock_Release(tb);
    }
    if (object) IInspectable_Release(object);

    IVector_UIElement_Release(children);
    IPanel_Release(grid_panel);
    IUIElement_Release(ui);
    IFrameworkElement_Release(fe);
    IFrameworkElement2_Release(page_grid);
    IInspectable_Release(root);
}

/* the Direct2D round: LinearGradientBrush, RenderTransform, Clip, at known places (an overlay at 600,300) */

static DWORD px(int x, int y)
{
    return pixels[y * width + x] & 0xffffff;
}

static BOOL close_rgb(DWORD c, int r, int g, int b, int tolerance)
{
    return abs((int)(c >> 16) - r) <= tolerance && abs((int)((c >> 8) & 0xff) - g) <= tolerance && abs((int)(c & 0xff) - b) <= tolerance;
}

static const WCHAR d2d_xaml[] =
    L"<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" Grid.RowSpan=\"2\" Margin=\"600,300,0,0\"\n"
    L"      HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\" Width=\"300\" Height=\"200\" Background=\"White\">\n"
    L"  <Border HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\" Width=\"100\" Height=\"20\">\n"
    L"    <Border.Background>\n"
    L"      <LinearGradientBrush StartPoint=\"0,0\" EndPoint=\"1,0\">\n"
    L"        <GradientStop Color=\"#FFFF0000\" Offset=\"0\"/>\n"
    L"        <GradientStop Color=\"#FF0000FF\" Offset=\"1\"/>\n"
    L"      </LinearGradientBrush>\n"
    L"    </Border.Background>\n"
    L"  </Border>\n"
    L"  <Border Margin=\"60,40,0,0\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\" Width=\"100\" Height=\"20\"\n"
    L"          Background=\"#FF00C000\" RenderTransformOrigin=\"0,0\">\n"
    L"    <Border.RenderTransform><RotateTransform Angle=\"90\"/></Border.RenderTransform>\n"
    L"  </Border>\n"
    L"  <Border Margin=\"150,0,0,0\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\" Width=\"100\" Height=\"40\"\n"
    L"          Background=\"#FFC0C000\">\n"
    L"    <Border.Clip><RectangleGeometry Rect=\"0,0,50,40\"/></Border.Clip>\n"
    L"  </Border>\n"
    L"  <Border Margin=\"150,60,0,0\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\" Width=\"20\" Height=\"20\"\n"
    L"          Background=\"#FF0000C0\">\n"
    L"    <Border.RenderTransform><TranslateTransform X=\"30\"/></Border.RenderTransform>\n"
    L"  </Border>\n"
    L"</Grid>\n";

static void check_d2d(struct main_page *page)
{
    IFrameworkElement2 *page_grid = page_root(page);
    IVector_UIElement *children;
    IInspectable *root;
    IPanel *panel;
    IUIElement *ui;
    HRESULT hr;
    DWORD c;
    int x = 600, y = 300;

    root = xaml_load(d2d_xaml, &hr);
    check(hr == S_OK && root, "XamlReader.Load of gradients, transforms and a clip, hr %#lx", hr);
    if (!root || !page_grid) return;
    IFrameworkElement2_QueryInterface(page_grid, &IID_IPanel, (void **)&panel);
    IPanel_get_Children(panel, &children);
    IInspectable_QueryInterface(root, &IID_IUIElement, (void **)&ui);
    IVector_UIElement_Append(children, ui);
    snapshot();

    c = px(x + 2, y + 10);
    check(close_rgb(c, 250, 0, 5, 12), "LinearGradientBrush: red at its start (%06lx)", c);
    c = px(x + 97, y + 10);
    check(close_rgb(c, 5, 0, 250, 12), "and blue at its end (%06lx)", c);
    c = px(x + 50, y + 10);
    check((c >> 16) > 60 && (c & 0xff) > 60 && ((c >> 8) & 0xff) < 30, "and a mix in the middle (%06lx)", c);

    /* rotated 90 degrees about its top left (60,40): it runs down from there, to the left of x = 60 */
    c = px(x + 50, y + 90);
    check(c == 0x00c000, "RotateTransform 90: the bar is turned to run downwards (%06lx at 50,90)", c);
    c = px(x + 110, y + 50);
    check(c != 0x00c000, "and is no longer where it was laid out (%06lx at 110,50)", c);

    c = px(x + 175, y + 20);
    check(c == 0xc0c000, "Clip: the left half of the clipped Border is drawn (%06lx)", c);
    c = px(x + 225, y + 20);
    check(c != 0xc0c000, "and its right half isn't (%06lx)", c);

    c = px(x + 190, y + 70);
    check(c == 0x0000c0, "TranslateTransform X=30 moves the square right (%06lx)", c);
    c = px(x + 155, y + 70);
    check(c != 0x0000c0, "from where it was laid out (%06lx)", c);

    IVector_UIElement_Release(children);
    IPanel_Release(panel);
    IUIElement_Release(ui);
    IFrameworkElement2_Release(page_grid);
    IInspectable_Release(root);
}

/* milestone 5 part 1: ScrollViewer, keyboard focus, Tab, key events (an overlay at 950,300) */

static int view_changed_count;
static HRESULT WINAPI view_changed_QueryInterface(IEventHandler_ScrollViewerViewChangedEventArgs *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IEventHandler_ScrollViewerViewChangedEventArgs)) { *out = iface; return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI view_changed_AddRef(IEventHandler_ScrollViewerViewChangedEventArgs *iface) { return 2; }
static ULONG WINAPI view_changed_Release(IEventHandler_ScrollViewerViewChangedEventArgs *iface) { return 1; }
static HRESULT WINAPI view_changed_Invoke(IEventHandler_ScrollViewerViewChangedEventArgs *iface, IInspectable *sender,
                                          IScrollViewerViewChangedEventArgs *args)
{
    boolean intermediate = TRUE;
    IScrollViewerViewChangedEventArgs_get_IsIntermediate(args, &intermediate);
    if (!intermediate) view_changed_count++;
    return S_OK;
}
static IEventHandler_ScrollViewerViewChangedEventArgsVtbl view_changed_vtbl =
    { view_changed_QueryInterface, view_changed_AddRef, view_changed_Release, view_changed_Invoke };
static IEventHandler_ScrollViewerViewChangedEventArgs on_view_changed = { &view_changed_vtbl };

static int key_down_count, last_key;
static HRESULT WINAPI key_QueryInterface(IKeyEventHandler *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IKeyEventHandler))
    { *out = iface; return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI key_AddRef(IKeyEventHandler *iface) { return 2; }
static ULONG WINAPI key_Release(IKeyEventHandler *iface) { return 1; }
static HRESULT WINAPI key_Invoke(IKeyEventHandler *iface, IInspectable *sender, IKeyRoutedEventArgs *args)
{
    __x_ABI_CWindows_CSystem_CVirtualKey key;
    IKeyRoutedEventArgs_get_Key(args, &key);
    key_down_count++;
    last_key = key;
    if (key == 'H') IKeyRoutedEventArgs_put_Handled(args, TRUE);   /* handled: no default action */
    return S_OK;
}
static IKeyEventHandlerVtbl key_vtbl = { key_QueryInterface, key_AddRef, key_Release, key_Invoke };
static IKeyEventHandler on_key_down = { &key_vtbl };

static const WCHAR scroll_xaml[] =
    L"<Grid xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"\n"
    L"      xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\" Grid.RowSpan=\"2\" Margin=\"950,300,0,0\"\n"
    L"      HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\" Width=\"300\" Height=\"200\" Background=\"White\">\n"
    L"  <ScrollViewer x:Name=\"Scroller\" Width=\"200\" Height=\"100\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\">\n"
    L"    <StackPanel>\n"
    L"      <Border Height=\"80\" Background=\"#FFFF0000\"/>\n"
    L"      <Border Height=\"80\" Background=\"#FF00FF00\"/>\n"
    L"      <Border Height=\"80\" Background=\"#FF0000FF\"/>\n"
    L"    </StackPanel>\n"
    L"  </ScrollViewer>\n"
    L"  <StackPanel x:Name=\"Keys\" Margin=\"210,0,0,0\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\">\n"
    L"    <Button x:Name=\"First\" Content=\"One\"/>\n"
    L"    <Button x:Name=\"Second\" Content=\"Two\"/>\n"
    L"    <Button x:Name=\"Skipped\" Content=\"No\" IsTabStop=\"False\"/>\n"
    L"  </StackPanel>\n"
    L"</Grid>\n";

static IInspectable *double_ref(double d)
{
    IPropertyValueStatics *pv = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
    IInspectable *value = NULL, *ref = NULL;

    IPropertyValueStatics_CreateDouble(pv, d, &value);
    if (value) IInspectable_QueryInterface(value, &IID_IReference_DOUBLE, (void **)&ref);
    if (value) IInspectable_Release(value);
    IPropertyValueStatics_Release(pv);
    return ref;
}

static void check_scroll_focus(struct main_page *page)
{
    IFrameworkElement2 *page_grid = page_root(page);
    IFocusManagerStatics *focus_manager = get_factory(L"Windows.UI.Xaml.Input.FocusManager", &IID_IFocusManagerStatics);
    IInspectable *root, *scroller, *first, *second, *skipped, *keys, *focused, *ref;
    IVector_UIElement *children;
    IScrollViewer *sv = NULL;
    IScrollViewer2 *sv2 = NULL;
    IControl *control = NULL;
    IButtonBase *bb = NULL;
    IUIElement *ui, *keys_ui = NULL;
    EventRegistrationToken token;
    int got_focus = 0, clicks = 0;
    struct counter_handler on_got_focus = { { &counter_vtbl }, &got_focus }, on_second_click = { { &counter_vtbl }, &clicks };
    Visibility visibility;
    FocusState state;
    boolean result;
    double d;
    HRESULT hr;
    POINT pt;
    DWORD c;
    IPanel *panel;
    int x = 950, y = 300;

    root = xaml_load(scroll_xaml, &hr);
    check(hr == S_OK && root, "XamlReader.Load of a ScrollViewer and buttons, hr %#lx", hr);
    check(focus_manager != NULL, "FocusManager's statics");
    if (!root || !page_grid || !focus_manager) return;
    IFrameworkElement2_QueryInterface(page_grid, &IID_IPanel, (void **)&panel);
    IPanel_get_Children(panel, &children);
    IInspectable_QueryInterface(root, &IID_IUIElement, (void **)&ui);
    IVector_UIElement_Append(children, ui);
    scroller = find_in(root, L"Scroller");
    first = find_in(root, L"First");
    second = find_in(root, L"Second");
    skipped = find_in(root, L"Skipped");
    keys = find_in(root, L"Keys");
    check(scroller && first && second && skipped && keys, "the named elements");
    if (!scroller || !first || !second || !skipped || !keys) goto done;
    IInspectable_QueryInterface(scroller, &IID_IScrollViewer, (void **)&sv);
    IInspectable_QueryInterface(scroller, &IID_IScrollViewer2, (void **)&sv2);
    check(sv && sv2, "ScrollViewer is an IScrollViewer and IScrollViewer2");
    if (!sv || !sv2) goto done;
    IScrollViewer_add_ViewChanged(sv, &on_view_changed, &token);
    snapshot();

    IScrollViewer_get_ExtentHeight(sv, &d);
    check(d == 240, "ExtentHeight is the content's 240 (%g)", d);
    IScrollViewer_get_ViewportHeight(sv, &d);
    check(d == 100, "ViewportHeight is the ScrollViewer's 100 (%g)", d);
    IScrollViewer_get_ScrollableHeight(sv, &d);
    check(d == 140, "ScrollableHeight is 140 (%g)", d);
    IScrollViewer_get_ExtentWidth(sv, &d);
    check(d == 200, "horizontal scrolling is disabled: ExtentWidth is the viewport's 200 (%g)", d);
    IScrollViewer_get_ComputedVerticalScrollBarVisibility(sv, &visibility);
    check(visibility == Visibility_Visible, "the vertical scroll bar is shown (%d)", visibility);
    c = px(x + 10, y + 10);
    check(c == 0xff0000, "before scrolling the first (red) item is at the top (%06lx)", c);
    c = px(x + 10, y + 120);
    check(c == 0xffffff, "and the content is clipped below the viewport (%06lx at 10,120)", c);

    ref = double_ref(100);
    IScrollViewer2_ChangeView(sv2, NULL, (IReference_DOUBLE *)ref, NULL, &result);
    if (ref) IInspectable_Release(ref);
    IScrollViewer_get_VerticalOffset(sv, &d);
    check(result && d == 100, "ChangeView(null, 100, null) scrolls to 100 (%g)", d);
    check(view_changed_count == 1, "and raises ViewChanged once (%d)", view_changed_count);
    snapshot();
    c = px(x + 10, y + 10);
    check(c == 0x00ff00, "after it the second (green) item is at the top (%06lx)", c);
    c = px(x + 10, y + 70);
    check(c == 0x0000ff, "and the third (blue) item below it (%06lx)", c);

    pt.x = x + 50;
    pt.y = y + 50;
    ClientToScreen(hwnd, &pt);
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
    IScrollViewer_get_VerticalOffset(sv, &d);
    check(d == 52, "the wheel turned up a notch scrolls up 48 px (%g)", d);
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -3 * WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
    IScrollViewer_get_VerticalOffset(sv, &d);
    check(d == 140, "and three notches down stop at the end, 140 (%g)", d);
    IScrollViewer_ScrollToVerticalOffset(sv, 0);
    IScrollViewer_get_VerticalOffset(sv, &d);
    check(d == 0, "ScrollToVerticalOffset(0) goes back to the top (%g)", d);

    /* focus */
    IInspectable_QueryInterface(first, &IID_IControl, (void **)&control);
    IInspectable_QueryInterface(first, &IID_IUIElement, (void **)&ui);
    IUIElement_add_GotFocus(ui, &on_got_focus.IRoutedEventHandler_iface, &token);
    IUIElement_Release(ui);
    result = FALSE;
    IControl_Focus(control, FocusState_Programmatic, &result);
    check(result, "Button.Focus(Programmatic) succeeds");
    IControl_get_FocusState(control, &state);
    check(state == FocusState_Programmatic, "and its FocusState is Programmatic (%d)", state);
    check(got_focus == 1, "and GotFocus is raised (%d)", got_focus);
    focused = NULL;
    IFocusManagerStatics_GetFocusedElement(focus_manager, &focused);
    check(focused == first, "FocusManager.GetFocusedElement is that button (%p, %p)", focused, first);
    if (focused) IInspectable_Release(focused);
    IControl_Release(control);

    IInspectable_QueryInterface(keys, &IID_IUIElement, (void **)&keys_ui);
    IUIElement_add_KeyDown(keys_ui, &on_key_down, &token);
    SendMessageW(hwnd, WM_KEYDOWN, VK_TAB, 0);
    SendMessageW(hwnd, WM_KEYUP, VK_TAB, 0);
    focused = NULL;
    IFocusManagerStatics_GetFocusedElement(focus_manager, &focused);
    check(focused == second, "Tab moves the focus to the next button (%p, %p)", focused, second);
    if (focused) IInspectable_Release(focused);
    check(key_down_count == 1 && last_key == VK_TAB, "and the focused button's KeyDown bubbles to its panel (%d, %#x)",
          key_down_count, last_key);
    IInspectable_QueryInterface(second, &IID_IControl, (void **)&control);
    IControl_get_FocusState(control, &state);
    check(state == FocusState_Keyboard, "focused by the keyboard (%d)", state);
    IControl_Release(control);
    snapshot();
    c = px(x + 211, y + 40);
    check(c == 0x000000, "and drawn with the focus visual (%06lx)", c);

    IInspectable_QueryInterface(second, &IID_IButtonBase, (void **)&bb);
    IButtonBase_add_Click(bb, &on_second_click.IRoutedEventHandler_iface, &token);
    SendMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);
    check(clicks == 0, "Space doesn't click until it is released (%d)", clicks);
    SendMessageW(hwnd, WM_KEYUP, VK_SPACE, 0);
    check(clicks == 1, "and clicks the focused button when it is (%d)", clicks);
    SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
    check(clicks == 2, "Enter clicks it too (%d)", clicks);
    SendMessageW(hwnd, WM_KEYDOWN, 'H', 0);
    SendMessageW(hwnd, WM_KEYDOWN, VK_TAB, 0);
    focused = NULL;
    IFocusManagerStatics_GetFocusedElement(focus_manager, &focused);
    check(focused && focused != skipped, "Tab skips a button with IsTabStop=False (%p, %p)", focused, skipped);
    if (focused) IInspectable_Release(focused);
    IButtonBase_Release(bb);

done:
    if (keys_ui) IUIElement_Release(keys_ui);
    if (sv) IScrollViewer_Release(sv);
    if (sv2) IScrollViewer2_Release(sv2);
    if (scroller) IInspectable_Release(scroller);
    if (first) IInspectable_Release(first);
    if (second) IInspectable_Release(second);
    if (skipped) IInspectable_Release(skipped);
    if (keys) IInspectable_Release(keys);
    IFocusManagerStatics_Release(focus_manager);
    IVector_UIElement_Release(children);
    IPanel_Release(panel);
    IFrameworkElement2_Release(page_grid);
    IInspectable_Release(root);
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
    check_toggles(the_page);
    check_text_markup(the_page);
    check_glyph_templates(the_page);
    check_corners(the_page);
    check_theme_change(the_page);
    check_loose_xaml();
    check_part6(the_page);
    check_d2d(the_page);
    check_scroll_focus(the_page);
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

    setvbuf( stdout, NULL, _IONBF, 0 );   /* in step with Wine's debug output */

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
