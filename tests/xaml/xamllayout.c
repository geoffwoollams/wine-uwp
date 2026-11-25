/* Milestone 2 test for wine-uwp's Windows.UI.Xaml: layout panels, Border, Button and pointer input, code-only.
 *
 * Builds this tree in OnLaunched (1280x720 window, light theme):
 *
 *   Grid  columns *, 2*   rows Auto, *
 *     (0,0) StackPanel, Margin 10: TextBlock "First" (20 px), TextBlock "Second", Button "Click me", Button "Disabled"
 *     (0,1) Border, blue, Padding 8: TextBlock "In a border" (white)
 *     (1,0) ColumnSpan 2: Border, green
 *
 * Then checks the arranged sizes, the drawn colours, hover and pressed looks, Click (sender, OriginalSource, a
 * disabled button that must not click), and Children collection behaviour. Exit status 0 only if every check
 * passed. Build: tests/xaml/build.sh. Run: tests/xaml/run.sh.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
#include "windows.ui.xaml.h"

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

static void *activate(const WCHAR *class_name, REFIID iid)
{
    IActivationFactory *factory = get_factory(class_name, &IID_IActivationFactory);
    IInspectable *object;
    void *out = NULL;
    HRESULT hr = IActivationFactory_ActivateInstance(factory, &object);
    if (hr != S_OK) { check(0, "activate %ls, hr %#lx", class_name, hr); return NULL; }
    IInspectable_QueryInterface(object, iid, &out);
    IInspectable_Release(object);
    IActivationFactory_Release(factory);
    return out;
}

static IUnknown *identity(void *iface)
{
    IUnknown *unk;
    IUnknown_QueryInterface((IUnknown *)iface, &IID_IUnknown, (void **)&unk);
    IUnknown_Release(unk);
    return unk;
}

static IBrush *solid(BYTE r, BYTE g, BYTE b)
{
    ISolidColorBrushFactory *factory = get_factory(L"Windows.UI.Xaml.Media.SolidColorBrush", &IID_ISolidColorBrushFactory);
    Color color = { 255, r, g, b };
    ISolidColorBrush *brush;
    IBrush *out;

    ISolidColorBrushFactory_CreateInstanceWithColor(factory, color, &brush);
    ISolidColorBrush_QueryInterface(brush, &IID_IBrush, (void **)&out);
    ISolidColorBrush_Release(brush);
    ISolidColorBrushFactory_Release(factory);
    return out;
}

static IInspectable *box_string(const WCHAR *s)
{
    IPropertyValueStatics *statics = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
    HSTRING str = hs(s);
    IInspectable *value = NULL;
    IPropertyValueStatics_CreateString(statics, str, &value);
    WindowsDeleteString(str);
    IPropertyValueStatics_Release(statics);
    return value;
}

static ITextBlock *text_block(const WCHAR *text, double size)
{
    ITextBlock *tb = activate(L"Windows.UI.Xaml.Controls.TextBlock", &IID_ITextBlock);
    HSTRING str = hs(text);
    ITextBlock_put_Text(tb, str);
    WindowsDeleteString(str);
    ITextBlock_put_FontSize(tb, size);
    return tb;
}

static void append(IPanel *panel, void *element)
{
    IVector_UIElement *children;
    IUIElement *ui;
    HRESULT hr;

    IPanel_get_Children(panel, &children);
    IUnknown_QueryInterface((IUnknown *)element, &IID_IUIElement, (void **)&ui);
    hr = IVector_UIElement_Append(children, ui);
    if (hr != S_OK) check(0, "Children.Append, hr %#lx", hr);
    IUIElement_Release(ui);
    IVector_UIElement_Release(children);
}

static double actual(void *element, BOOL height)
{
    IFrameworkElement *fe;
    DOUBLE v;
    IUnknown_QueryInterface((IUnknown *)element, &IID_IFrameworkElement, (void **)&fe);
    if (height) IFrameworkElement_get_ActualHeight(fe, &v);
    else IFrameworkElement_get_ActualWidth(fe, &v);
    IFrameworkElement_Release(fe);
    return v;
}

static void grid_cell(IGridStatics *statics, void *element, INT32 row, INT32 column, INT32 column_span)
{
    IFrameworkElement *fe;
    IUnknown_QueryInterface((IUnknown *)element, &IID_IFrameworkElement, (void **)&fe);
    IGridStatics_SetRow(statics, fe, row);
    IGridStatics_SetColumn(statics, fe, column);
    IGridStatics_SetColumnSpan(statics, fe, column_span);
    IFrameworkElement_Release(fe);
}

/* ---- the tree ---- */

static IGrid *grid;
static IStackPanel *stack;
static ITextBlock *first, *second;
static IButton *button, *disabled;
static IBorder *blue, *green;
static int clicks, disabled_clicks;
static BOOL click_sender_ok, click_source_ok;

/* ---- Click handler ---- */

struct click_handler
{
    IRoutedEventHandler IRoutedEventHandler_iface;
    int *count;
};

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
    struct click_handler *h = CONTAINING_RECORD(iface, struct click_handler, IRoutedEventHandler_iface);
    IInspectable *source = NULL;
    WCHAR text[32];
    HSTRING str;

    (*h->count)++;
    if (h->count != &clicks) return S_OK;
    click_sender_ok = identity(sender) == identity(button);
    IRoutedEventArgs_get_OriginalSource(args, &source);
    click_source_ok = source && identity(source) == identity(button);
    if (source) IInspectable_Release(source);
    swprintf(text, ARRAYSIZE(text), L"Clicked %d", clicks);
    str = hs(text);
    ITextBlock_put_Text(first, str);   /* what an app's handler does */
    WindowsDeleteString(str);
    return S_OK;
}

static IRoutedEventHandlerVtbl click_vtbl = { click_QueryInterface, click_AddRef, click_Release, click_Invoke };
static struct click_handler click_handler = { { &click_vtbl }, &clicks };
static struct click_handler disabled_handler = { { &click_vtbl }, &disabled_clicks };

/* ---- pixels ---- */

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
    DeleteDC(mem);
    DeleteObject(bitmap);
    ReleaseDC(hwnd, hdc);
}

static DWORD pixel(double x, double y)
{
    int ix = (int)x, iy = (int)y;
    if (ix < 0 || iy < 0 || ix >= width || iy >= height) return 0xdeadbeef;
    return pixels[iy * width + ix] & 0xffffff;
}

static void save_screenshot(const char *name)
{
    BITMAPINFOHEADER bih = { sizeof(bih), width, -height, 1, 32 };
    BITMAPFILEHEADER file = { 0x4d42, sizeof(file) + sizeof(bih) + width * height * 4, 0, 0, sizeof(file) + sizeof(bih) };
    HANDLE f;
    DWORD written;

    if (!GetEnvironmentVariableA("XAMLHELLO_SCREENSHOT", NULL, 0)) return;
    f = CreateFileA(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, &file, sizeof(file), &written, NULL);
    WriteFile(f, &bih, sizeof(bih), &written, NULL);
    WriteFile(f, pixels, width * height * 4, &written, NULL);
    CloseHandle(f);
}

static void mouse(UINT msg, double x, double y)
{
    SendMessageW(hwnd, msg, msg == WM_LBUTTONDOWN ? MK_LBUTTON : 0, MAKELPARAM((int)x, (int)y));
}

#define close_to(a, b) (fabs((a) - (b)) < 1.0)

static void CALLBACK verify(HWND unused, UINT msg, UINT_PTR id, DWORD time)
{
    IApplicationStatics *app_statics;
    IApplication *app;
    IVector_RowDefinition *rows;
    IVector_UIElement *children;
    IRowDefinition *row;
    IFrameworkElement *fe;
    IDependencyObject *parent;
    IIterable_UIElement *iterable;
    IIterator_UIElement *iterator;
    IStackPanel *other;
    IUIElement *ui;
    boolean has;
    DOUBLE row0, row1;
    Size desired_before, desired_after;
    double bx, by, bw, bh, cx, cy, h1, h2;
    UINT32 n, count;
    HRESULT hr;

    KillTimer(NULL, id);
    EnumThreadWindows(GetCurrentThreadId(), find_core_hwnd, (LPARAM)&hwnd);
    snapshot();
    save_screenshot("xamllayout.bmp");
    printf("client %dx%d\n", width, height);

    /* sizes from Grid star and Auto sizing */
    check(close_to(actual(grid, FALSE), 1280) && close_to(actual(grid, TRUE), 720), "Grid fills the window (%.1fx%.1f)",
          actual(grid, FALSE), actual(grid, TRUE));
    check(close_to(actual(blue, FALSE), 1280.0 * 2 / 3), "2* column is two thirds wide (%.2f)", actual(blue, FALSE));
    IGrid_get_RowDefinitions(grid, &rows);
    IVector_RowDefinition_GetAt(rows, 0, &row);
    IRowDefinition_get_ActualHeight(row, &row0);
    IRowDefinition_Release(row);
    IVector_RowDefinition_GetAt(rows, 1, &row);
    IRowDefinition_get_ActualHeight(row, &row1);
    IRowDefinition_Release(row);
    IVector_RowDefinition_Release(rows);
    check(close_to(row0, actual(stack, TRUE) + 20), "Auto row is the StackPanel plus its margin (%.1f, panel %.1f)", row0, actual(stack, TRUE));
    check(close_to(row0 + row1, 720), "* row takes the rest (%.1f + %.1f)", row0, row1);
    check(close_to(actual(green, TRUE), row1) && close_to(actual(green, FALSE), 1280), "ColumnSpan 2 Border fills the bottom row (%.1fx%.1f)",
          actual(green, FALSE), actual(green, TRUE));
    check(close_to(actual(blue, TRUE), row0), "Border stretches to the Auto row's height (%.1f)", actual(blue, TRUE));
    h1 = actual(first, TRUE);
    h2 = actual(second, TRUE);
    bw = actual(button, FALSE);
    bh = actual(button, TRUE);
    check(h1 > h2 && h2 > 10, "TextBlock heights follow FontSize (%.1f, %.1f)", h1, h2);
    check(close_to(bh, h2 + 4 + 5 + 4) || (bh > 28 && bh < 40), "Button height is content + padding + border (%.1f)", bh);
    check(bw < 200 && bw > 40, "Button sizes to its content, left-aligned (%.1f wide)", bw);
    check(close_to(actual(stack, FALSE), 1280.0 / 3 - 20), "StackPanel stretches across its column (%.1f)", actual(stack, FALSE));

    /* colours */
    check(pixel(1280 * 2.0 / 3 + 400, 10) == 0x0000ff, "blue Border drawn (%06lx)", pixel(1280 * 2.0 / 3 + 400, 10));
    check(pixel(640, row0 + row1 / 2) == 0x00ff00, "green Border drawn (%06lx)", pixel(640, row0 + row1 / 2));
    bx = 10;
    by = 10 + h1 + h2;
    cx = bx + bw / 2;
    cy = by + bh / 2;
    check(pixel(bx + 3, cy) == 0xcccccc, "Button normal background #cccccc (%06lx)", pixel(bx + 3, cy));
    check(pixel(bx, cy) == 0xcccccc, "Button normal border is not drawn (%06lx)", pixel(bx, cy));

    /* hover */
    mouse(WM_MOUSEMOVE, cx, cy);
    snapshot();
    check(pixel(bx, cy) == 0x999999 && pixel(bx + 3, cy) == 0xcccccc, "hover draws the 2 px border (%06lx, %06lx)", pixel(bx, cy), pixel(bx + 3, cy));

    {
        IUIElement *ui_first;
        ITextBlock_QueryInterface(first, &IID_IUIElement, (void **)&ui_first);
        IUIElement_get_DesiredSize(ui_first, &desired_before);
        IUIElement_Release(ui_first);
    }

    /* press, release */
    mouse(WM_LBUTTONDOWN, cx, cy);
    snapshot();
    check(pixel(bx + 3, cy) == 0x999999, "pressed background #999999 (%06lx)", pixel(bx + 3, cy));
    check(clicks == 0, "no Click on press (ClickMode Release)");
    mouse(WM_LBUTTONUP, cx, cy);
    check(clicks == 1, "Click raised on release (%d)", clicks);
    check(click_sender_ok && click_source_ok, "Click sender and OriginalSource are the Button");
    snapshot();
    save_screenshot("xamllayout-clicked.bmp");
    {
        HSTRING str;
        ITextBlock_get_Text(first, &str);
        check(!wcscmp(WindowsGetStringRawBuffer(str, NULL), L"Clicked 1"), "the handler changed the TextBlock");
        WindowsDeleteString(str);
    }
    {
        IUIElement *ui_first;
        ITextBlock_QueryInterface(first, &IID_IUIElement, (void **)&ui_first);
        IUIElement_get_DesiredSize(ui_first, &desired_after);
        IUIElement_Release(ui_first);
        check(desired_after.Width > desired_before.Width + 20, "relayout measured the new text (desired width %.1f -> %.1f)",
              desired_before.Width, desired_after.Width);
    }

    /* press inside, release outside: no click */
    mouse(WM_LBUTTONDOWN, cx, cy);
    mouse(WM_MOUSEMOVE, cx + 500, cy);
    mouse(WM_LBUTTONUP, cx + 500, cy);
    check(clicks == 1, "releasing outside the button doesn't click (%d)", clicks);

    /* disabled button */
    by += bh;
    mouse(WM_MOUSEMOVE, bx + 10, by + bh / 2);
    mouse(WM_LBUTTONDOWN, bx + 10, by + bh / 2);
    mouse(WM_LBUTTONUP, bx + 10, by + bh / 2);
    check(disabled_clicks == 0, "a disabled button doesn't click");
    snapshot();
    check(pixel(bx + 3, by + bh / 2) == 0xcccccc, "disabled button keeps the normal background (%06lx)", pixel(bx + 3, by + bh / 2));

    /* the Children collection */
    IPanel *panel;
    IStackPanel_QueryInterface(stack, &IID_IPanel, (void **)&panel);
    IPanel_get_Children(panel, &children);
    IVector_UIElement_get_Size(children, &n);
    check(n == 4, "StackPanel has 4 children (%u)", n);
    IVector_UIElement_QueryInterface(children, &IID_IIterable_UIElement, (void **)&iterable);
    IIterable_UIElement_First(iterable, &iterator);
    for (count = 0, IIterator_UIElement_get_HasCurrent(iterator, &has); has; IIterator_UIElement_MoveNext(iterator, &has)) count++;
    check(count == 4, "iterating Children gives 4 (%u)", count);
    IIterator_UIElement_Release(iterator);
    IIterable_UIElement_Release(iterable);

    ITextBlock_QueryInterface(second, &IID_IFrameworkElement, (void **)&fe);
    IFrameworkElement_get_Parent(fe, &parent);
    check(parent && identity(parent) == identity(stack), "TextBlock.Parent is the StackPanel");
    if (parent) IDependencyObject_Release(parent);

    other = activate(L"Windows.UI.Xaml.Controls.StackPanel", &IID_IStackPanel);
    {
        IPanel *other_panel;
        IVector_UIElement *other_children;
        IStackPanel_QueryInterface(other, &IID_IPanel, (void **)&other_panel);
        IPanel_get_Children(other_panel, &other_children);
        ITextBlock_QueryInterface(second, &IID_IUIElement, (void **)&ui);
        hr = IVector_UIElement_Append(other_children, ui);
        check(FAILED(hr), "an element can't have two parents, hr %#lx", hr);
        IVector_UIElement_RemoveAt(children, 1);
        IFrameworkElement_get_Parent(fe, &parent);
        check(!parent, "removing a child clears its Parent");
        hr = IVector_UIElement_Append(other_children, ui);
        check(hr == S_OK, "and it can then join another panel, hr %#lx", hr);
        IVector_UIElement_Release(other_children);
        IPanel_Release(other_panel);
        IUIElement_Release(ui);
    }
    IStackPanel_Release(other);
    IFrameworkElement_Release(fe);
    IVector_UIElement_Release(children);
    IPanel_Release(panel);

    app_statics = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationStatics);
    IApplicationStatics_get_Current(app_statics, &app);
    printf("exiting\n");
    IApplication_Exit(app);
    IApplication_Release(app);
    IApplicationStatics_Release(app_statics);
}

/* ---- the App ---- */

static void build_tree(IWindow *window)
{
    IGridStatics *grid_statics = get_factory(L"Windows.UI.Xaml.Controls.Grid", &IID_IGridStatics);
    IGridFactory *grid_factory = get_factory(L"Windows.UI.Xaml.Controls.Grid", &IID_IGridFactory);
    IButtonFactory *button_factory = get_factory(L"Windows.UI.Xaml.Controls.Button", &IID_IButtonFactory);
    IVector_RowDefinition *rows;
    IVector_ColumnDefinition *columns;
    IRowDefinition *row;
    IColumnDefinition *column;
    IContentControl *content;
    IButtonBase *base;
    IControl *control;
    IFrameworkElement *fe;
    IInspectable *inner, *str;
    ITextBlock *label;
    IPanel *panel;
    IUIElement *ui;
    EventRegistrationToken token;
    GridLength length;
    Thickness margin = { 10, 10, 10, 10 }, padding = { 8, 8, 8, 8 };
    HRESULT hr;

    hr = IGridFactory_CreateInstance(grid_factory, NULL, &inner, &grid);
    check(hr == S_OK && grid && !inner, "Grid via IGridFactory without an outer object, hr %#lx", hr);
    IGrid_get_RowDefinitions(grid, &rows);
    IGrid_get_ColumnDefinitions(grid, &columns);
    row = activate(L"Windows.UI.Xaml.Controls.RowDefinition", &IID_IRowDefinition);
    length.Value = 0; length.GridUnitType = GridUnitType_Auto;
    IRowDefinition_put_Height(row, length);
    IVector_RowDefinition_Append(rows, row);
    IRowDefinition_Release(row);
    row = activate(L"Windows.UI.Xaml.Controls.RowDefinition", &IID_IRowDefinition);   /* default: 1* */
    IVector_RowDefinition_Append(rows, row);
    IRowDefinition_Release(row);
    column = activate(L"Windows.UI.Xaml.Controls.ColumnDefinition", &IID_IColumnDefinition);
    IVector_ColumnDefinition_Append(columns, column);
    IColumnDefinition_Release(column);
    column = activate(L"Windows.UI.Xaml.Controls.ColumnDefinition", &IID_IColumnDefinition);
    length.Value = 2; length.GridUnitType = GridUnitType_Star;
    IColumnDefinition_put_Width(column, length);
    IVector_ColumnDefinition_Append(columns, column);
    IColumnDefinition_Release(column);
    IVector_RowDefinition_Release(rows);
    IVector_ColumnDefinition_Release(columns);
    IGrid_QueryInterface(grid, &IID_IPanel, (void **)&panel);

    stack = activate(L"Windows.UI.Xaml.Controls.StackPanel", &IID_IStackPanel);
    IStackPanel_QueryInterface(stack, &IID_IFrameworkElement, (void **)&fe);
    IFrameworkElement_put_Margin(fe, margin);
    IFrameworkElement_Release(fe);
    grid_cell(grid_statics, stack, 0, 0, 1);
    append(panel, stack);
    {
        IPanel *stack_panel;
        IStackPanel_QueryInterface(stack, &IID_IPanel, (void **)&stack_panel);
        first = text_block(L"First", 20);
        second = text_block(L"Second", 14);
        append(stack_panel, first);
        append(stack_panel, second);

        hr = IButtonFactory_CreateInstance(button_factory, NULL, &inner, &button);
        check(hr == S_OK && button, "Button via IButtonFactory, hr %#lx", hr);
        IButton_QueryInterface(button, &IID_IContentControl, (void **)&content);
        str = box_string(L"Click me");
        IContentControl_put_Content(content, str);
        IInspectable_Release(str);
        IContentControl_Release(content);
        IButton_QueryInterface(button, &IID_IButtonBase, (void **)&base);
        hr = IButtonBase_add_Click(base, &click_handler.IRoutedEventHandler_iface, &token);
        check(hr == S_OK, "Button.Click += handler");
        IButtonBase_Release(base);
        append(stack_panel, button);

        IButtonFactory_CreateInstance(button_factory, NULL, &inner, &disabled);
        IButton_QueryInterface(disabled, &IID_IContentControl, (void **)&content);
        str = box_string(L"Disabled");
        IContentControl_put_Content(content, str);
        IInspectable_Release(str);
        IContentControl_Release(content);
        IButton_QueryInterface(disabled, &IID_IButtonBase, (void **)&base);
        IButtonBase_add_Click(base, &disabled_handler.IRoutedEventHandler_iface, &token);
        IButtonBase_Release(base);
        IButton_QueryInterface(disabled, &IID_IControl, (void **)&control);
        IControl_put_IsEnabled(control, FALSE);
        IControl_Release(control);
        append(stack_panel, disabled);
        IPanel_Release(stack_panel);
    }

    blue = activate(L"Windows.UI.Xaml.Controls.Border", &IID_IBorder);
    IBorder_put_Background(blue, solid(0, 0, 255));
    IBorder_put_Padding(blue, padding);
    label = text_block(L"In a border", 16);
    ITextBlock_put_Foreground(label, solid(255, 255, 255));
    ITextBlock_QueryInterface(label, &IID_IUIElement, (void **)&ui);
    IBorder_put_Child(blue, ui);
    IUIElement_Release(ui);
    ITextBlock_Release(label);
    grid_cell(grid_statics, blue, 0, 1, 1);
    append(panel, blue);

    green = activate(L"Windows.UI.Xaml.Controls.Border", &IID_IBorder);
    IBorder_put_Background(green, solid(0, 255, 0));
    grid_cell(grid_statics, green, 1, 0, 2);
    append(panel, green);

    IGrid_QueryInterface(grid, &IID_IUIElement, (void **)&ui);
    IWindow_put_Content(window, ui);
    IUIElement_Release(ui);
    IPanel_Release(panel);
    IButtonFactory_Release(button_factory);
    IGridFactory_Release(grid_factory);
    IGridStatics_Release(grid_statics);
}

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
    return IInspectable_QueryInterface(app->inner, iid, out);
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
static HRESULT WINAPI app_GetRuntimeClassName(IApplicationOverrides *iface, HSTRING *name) { *name = hs(L"XamlLayout.App"); return S_OK; }
static HRESULT WINAPI app_GetTrustLevel(IApplicationOverrides *iface, TrustLevel *level) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI app_OnActivated(IApplicationOverrides *iface, IActivatedEventArgs *args) { return S_OK; }

static HRESULT WINAPI app_OnLaunched(IApplicationOverrides *iface, ILaunchActivatedEventArgs *args)
{
    IWindowStatics *window_statics = get_factory(L"Windows.UI.Xaml.Window", &IID_IWindowStatics);
    IWindow *window;

    IWindowStatics_get_Current(window_statics, &window);
    IWindowStatics_Release(window_statics);
    build_tree(window);
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

static HRESULT WINAPI init_QueryInterface(IApplicationInitializationCallback *iface, REFIID iid, void **out)
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI init_AddRef(IApplicationInitializationCallback *iface) { return 2; }
static ULONG WINAPI init_Release(IApplicationInitializationCallback *iface) { return 1; }

static HRESULT WINAPI init_Invoke(IApplicationInitializationCallback *iface, IApplicationInitializationCallbackParams *params)
{
    IApplicationFactory *factory = get_factory(L"Windows.UI.Xaml.Application", &IID_IApplicationFactory);
    struct app *app = calloc(1, sizeof(*app));
    IApplication *base;

    app->IApplicationOverrides_iface.lpVtbl = &app_vtbl;
    app->ref = 1;
    IApplicationFactory_CreateInstance(factory, (IInspectable *)&app->IApplicationOverrides_iface, &app->inner, &base);
    IApplication_Release(base);
    IApplicationFactory_Release(factory);
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
