#include "pch.h"
#include "bmp_from_svg.h"
#include "resource.h"

#include <windowsx.h>

#include <cmath>
#include <wchar.h>

////////////////////////////////////////////////////////////////////////////////

static const COLORREF BG_COLOR = RGB(0xff, 0xff, 0xff);
extern "C" int _fltused = 0;
static ATOM wnd_class = 0;

////////////////////////////////////////////////////////////////////////////////

extern "C" BYTE __ImageBase;

inline HINSTANCE hinst()
{
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_DETACH)
    {
        if (wnd_class)
        {
            UnregisterClass(MAKEINTRESOURCE(wnd_class), hinst());
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

// Win32 has no public IDC_CLOSEDHAND constant, and there is no documented
// way to get one from the system - even wxWidgets, a mature cross-platform
// GUI library, only implements a distinct open/closed hand pair on macOS and
// falls back to the same open-hand cursor on Windows (see wxWidgets issue
// #10360, src/msw/cursor.cpp). So both hand cursors are bundled as real
// plugin resources (src/res/{open,closed}_hand.cur, declared in
// svg_wlx.rc) rather than relying on an undocumented system resource ID.
static HCURSOR closed_hand_cursor()
{
    static HCURSOR cached = LoadCursor(hinst(), MAKEINTRESOURCE(IDC_CLOSED_HAND));
    return cached;
}

static HCURSOR open_hand_cursor()
{
    static HCURSOR cached = LoadCursor(hinst(), MAKEINTRESOURCE(IDC_OPEN_HAND));
    return cached;
}

////////////////////////////////////////////////////////////////////////////////

static const float ZOOM_MIN  = 0.1f;
static const float ZOOM_MAX  = 8.0f;
static const float ZOOM_STEP = 1.1f;  // multiplier per wheel notch / +- press

struct SvgCtxt
{
    HBITMAP bitmap;
    LONG    width;         // rendered bitmap size (at current zoom)
    LONG    height;
    LONG    client_cx;     // client size the bitmap was rendered for
    LONG    client_cy;
    float   zoom;          // 1.0 = fit-to-window
    LONG    pan_x;         // top-left of the visible viewport into the
    LONG    pan_y;         // (possibly larger-than-window) zoomed bitmap
    int     wheel_accum;   // sub-notch WM_MOUSEWHEEL delta accumulator
    bool    panning;       // true while dragging with the left button held
    POINT   pan_anchor;    // client-space point where the current drag started
    LONG    pan_anchor_x;  // pan_x/pan_y at the start of the current drag
    LONG    pan_anchor_y;
    POINT   last_mouse;    // last known client-space mouse position, used as
                            // the zoom anchor for +/- keyboard zooming
    WCHAR   fname[MAX_PATH];
};

inline SvgCtxt* get_ctxt(HWND hwnd)
{
    return reinterpret_cast<SvgCtxt*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

static void clamp_pan(SvgCtxt* ctxt, LONG cx, LONG cy)
{
    auto max_x = (ctxt->width > cx) ? (ctxt->width - cx) : 0;
    auto max_y = (ctxt->height > cy) ? (ctxt->height - cy) : 0;
    if (ctxt->pan_x < 0) ctxt->pan_x = 0;
    if (ctxt->pan_y < 0) ctxt->pan_y = 0;
    if (ctxt->pan_x > max_x) ctxt->pan_x = max_x;
    if (ctxt->pan_y > max_y) ctxt->pan_y = max_y;
}

// Re-rasterizes at (client size * zoom) unless that exact size was already
// rendered. Vector re-render (rather than stretching the old bitmap) is what
// keeps the image crisp across resizes, DPI changes, and zoom changes alike -
// zooming in just asks the renderer for a bigger bitmap, and WM_PAINT blits
// only the currently-panned-to sub-rectangle of it.
static void render_for_client(SvgCtxt* ctxt, HWND hwnd, bool force = false)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    auto cx = rc.right - rc.left;
    auto cy = rc.bottom - rc.top;
    if (cx <= 0 || cy <= 0)
    {
        return;
    }
    auto target_w = LONG(cx * ctxt->zoom + 0.5f);
    auto target_h = LONG(cy * ctxt->zoom + 0.5f);
    if (!force && ctxt->bitmap && cx == ctxt->client_cx && cy == ctxt->client_cy
        && target_w == ctxt->width && target_h == ctxt->height)
    {
        return;
    }
    if (ctxt->bitmap)
    {
        DeleteObject(ctxt->bitmap);
    }
    ctxt->bitmap = bitmap_from_svg(
        ctxt->fname,
        target_w,
        target_h,
        BG_COLOR,
        ctxt->width,
        ctxt->height
        );
    ctxt->client_cx = cx;
    ctxt->client_cy = cy;
    clamp_pan(ctxt, cx, cy);
}

// Total Commander's Lister keeps its own "Fit image to window" menu
// checkbox and F-key accelerator; it never delivers a raw keystroke to us
// for it (see ListSendCommand's lc_newparams handling below). Instead it
// expects the plugin to proactively report its fit state via this
// WM_COMMAND, per the WLX SDK (wm_command.htm, itm_fit). Without this, TC's
// checkbox silently drifts out of sync with our real zoom (e.g. after a
// wheel/drag zoom it never finds out we're no longer "fit"), so the first
// F press just toggles TC's stale checkbox to a state that doesn't set
// lcp_fittowindow, and the user has to press F twice to see any effect.
static void notify_fit_state(HWND hwnd, bool fit)
{
    PostMessage(GetParent(hwnd), WM_COMMAND, MAKELONG(fit ? 1 : 0, itm_fit), (LPARAM)hwnd);
}

static void reset_to_fit(SvgCtxt* ctxt, HWND hwnd)
{
    ctxt->zoom = 1.0f;
    ctxt->pan_x = 0;
    ctxt->pan_y = 0;
    render_for_client(ctxt, hwnd, true);
    InvalidateRect(hwnd, nullptr, false);
    notify_fit_state(hwnd, true);
}

////////////////////////////////////////////////////////////////////////////////

// Small always-visible on-screen zoom toolbar (zoom in / zoom out / fit),
// top-right corner, for mouse-only use alongside the wheel/keyboard/drag
// controls.
static const LONG TOOLBAR_BTN_SIZE   = 28;
static const LONG TOOLBAR_BTN_GAP    = 4;
static const LONG TOOLBAR_BTN_MARGIN = 8;

static void get_toolbar_rects(HWND hwnd, RECT& r_zoom_in, RECT& r_zoom_out, RECT& r_fit)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    r_fit.top = TOOLBAR_BTN_MARGIN;
    r_fit.bottom = TOOLBAR_BTN_MARGIN + TOOLBAR_BTN_SIZE;
    r_fit.right = rc.right - TOOLBAR_BTN_MARGIN;
    r_fit.left = r_fit.right - TOOLBAR_BTN_SIZE;

    r_zoom_out.top = r_fit.top;
    r_zoom_out.bottom = r_fit.bottom;
    r_zoom_out.right = r_fit.left - TOOLBAR_BTN_GAP;
    r_zoom_out.left = r_zoom_out.right - TOOLBAR_BTN_SIZE;

    r_zoom_in.top = r_fit.top;
    r_zoom_in.bottom = r_fit.bottom;
    r_zoom_in.right = r_zoom_out.left - TOOLBAR_BTN_GAP;
    r_zoom_in.left = r_zoom_in.right - TOOLBAR_BTN_SIZE;
}

enum ToolbarIcon { ICON_ZOOM_IN, ICON_ZOOM_OUT, ICON_FIT };

static void draw_toolbar_button(HDC hdc, const RECT& r, ToolbarIcon icon)
{
    auto bg = CreateSolidBrush(RGB(0xf0, 0xf0, 0xf0));
    auto border = CreatePen(PS_SOLID, 1, RGB(0xa0, 0xa0, 0xa0));
    auto old_brush = SelectObject(hdc, bg);
    auto old_pen = SelectObject(hdc, border);
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);

    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    auto icon_pen = CreatePen(PS_SOLID, 2, RGB(0x40, 0x40, 0x40));
    SelectObject(hdc, icon_pen);

    if (icon == ICON_FIT)
    {
        // Four corner brackets, suggesting "fit to window".
        const LONG len = 6, off = 6;
        MoveToEx(hdc, r.left + off, r.top + off + len, nullptr);
        LineTo(hdc, r.left + off, r.top + off);
        LineTo(hdc, r.left + off + len, r.top + off);

        MoveToEx(hdc, r.right - off - len, r.top + off, nullptr);
        LineTo(hdc, r.right - off, r.top + off);
        LineTo(hdc, r.right - off, r.top + off + len);

        MoveToEx(hdc, r.left + off, r.bottom - off - len, nullptr);
        LineTo(hdc, r.left + off, r.bottom - off);
        LineTo(hdc, r.left + off + len, r.bottom - off);

        MoveToEx(hdc, r.right - off - len, r.bottom - off, nullptr);
        LineTo(hdc, r.right - off, r.bottom - off);
        LineTo(hdc, r.right - off, r.bottom - off - len);
    }
    else
    {
        // Magnifying glass (circle + handle) with a +/- inside the lens.
        const LONG radius = 6;
        auto lens_cx = (r.left + r.right) / 2 - 2;
        auto lens_cy = (r.top + r.bottom) / 2 - 2;
        Ellipse(hdc, lens_cx - radius, lens_cy - radius, lens_cx + radius, lens_cy + radius);
        MoveToEx(hdc, lens_cx + radius - 1, lens_cy + radius - 1, nullptr);
        LineTo(hdc, lens_cx + radius + 6, lens_cy + radius + 6);

        MoveToEx(hdc, lens_cx - 3, lens_cy, nullptr);
        LineTo(hdc, lens_cx + 3, lens_cy);
        if (icon == ICON_ZOOM_IN)
        {
            MoveToEx(hdc, lens_cx, lens_cy - 3, nullptr);
            LineTo(hdc, lens_cx, lens_cy + 3);
        }
    }

    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(bg);
    DeleteObject(border);
    DeleteObject(icon_pen);
}

// Single place zoom actually changes - the mouse wheel, +/- keys, and the
// on-screen toolbar buttons all route through this directly (no
// SendMessage-to-self indirection), so all three input paths behave
// identically. `anchor` is a point in this window's client space; the SVG
// content under it stays fixed on screen across the zoom change.
static void zoom_by(SvgCtxt* ctxt, HWND hwnd, int steps, POINT anchor)
{
    if (!ctxt || !ctxt->bitmap || steps == 0)
    {
        return;
    }

    auto new_zoom = ctxt->zoom * powf(ZOOM_STEP, float(steps));
    if (new_zoom < ZOOM_MIN) new_zoom = ZOOM_MIN;
    if (new_zoom > ZOOM_MAX) new_zoom = ZOOM_MAX;
    if (new_zoom == ctxt->zoom)
    {
        return;
    }

    RECT rc;
    GetClientRect(hwnd, &rc);
    auto cw = rc.right - rc.left;
    auto ch = rc.bottom - rc.top;
    auto dest_x = (cw > ctxt->width) ? (cw - ctxt->width) / 2 : 0;
    auto dest_y = (ch > ctxt->height) ? (ch - ctxt->height) / 2 : 0;

    auto anchor_x = anchor.x - dest_x;
    auto anchor_y = anchor.y - dest_y;
    if (anchor_x < 0) anchor_x = 0;
    if (anchor_x > ctxt->width) anchor_x = ctxt->width;
    if (anchor_y < 0) anchor_y = 0;
    if (anchor_y > ctxt->height) anchor_y = ctxt->height;
    auto frac_x = float(ctxt->pan_x + anchor_x) / float(ctxt->width);
    auto frac_y = float(ctxt->pan_y + anchor_y) / float(ctxt->height);

    ctxt->zoom = new_zoom;
    render_for_client(ctxt, hwnd, true);

    // The letterbox offset can genuinely change between a "fit-to-window"
    // (centered) state and a "filled" (zoomed past window size) state, so it
    // must be recomputed against the post-render bitmap size, not reused
    // from before the zoom change.
    auto dest_x2 = (cw > ctxt->width) ? (cw - ctxt->width) / 2 : 0;
    auto dest_y2 = (ch > ctxt->height) ? (ch - ctxt->height) / 2 : 0;

    ctxt->pan_x = LONG(frac_x * ctxt->width) - (anchor.x - dest_x2);
    ctxt->pan_y = LONG(frac_y * ctxt->height) - (anchor.y - dest_y2);
    clamp_pan(ctxt, ctxt->client_cx, ctxt->client_cy);

    InvalidateRect(hwnd, nullptr, false);
    notify_fit_state(hwnd, false);
}

// Shared by WM_LBUTTONDOWN and WM_LBUTTONDBLCLK: our window class has
// CS_DBLCLKS, so two quick clicks on the same toolbar button (e.g. mashing
// zoom+) arrive as WM_LBUTTONDOWN followed by WM_LBUTTONDBLCLK rather than
// two WM_LBUTTONDOWNs. Both must hit-test the toolbar the same way, or a
// double-click lands as a full reset-to-fit and zoom appears to "bounce"
// back to 1.0 instead of continuing to zoom in/out. Returns true if the
// point was over a toolbar button (and the corresponding action was taken).
static bool try_toolbar_click(SvgCtxt* ctxt, HWND hwnd, POINT pt)
{
    RECT r_in, r_out, r_fit;
    get_toolbar_rects(hwnd, r_in, r_out, r_fit);
    if (PtInRect(&r_in, pt))
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
        zoom_by(ctxt, hwnd, 1, center);
        return true;
    }
    if (PtInRect(&r_out, pt))
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
        zoom_by(ctxt, hwnd, -1, center);
        return true;
    }
    if (PtInRect(&r_fit, pt))
    {
        reset_to_fit(ctxt, hwnd);
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC hdc;
    RECT rc;

    auto* ctxt = get_ctxt(hwnd);

    switch (msg)
    {
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        if (ctxt && ctxt->bitmap)
        {
            HDC memDC = CreateCompatibleDC(hdc);
            HGDIOBJ oldBmp = SelectObject(memDC, ctxt->bitmap);

            // Already rendered at the exact size we want to show (see
            // render_for_client) - just crop-blit the currently panned-to
            // sub-rectangle, 1:1, no GDI stretching needed.
            auto cw = rc.right - rc.left;
            auto ch = rc.bottom - rc.top;
            auto visible_w = (ctxt->width < cw) ? ctxt->width : cw;
            auto visible_h = (ctxt->height < ch) ? ctxt->height : ch;
            auto dest_x = (cw > ctxt->width) ? (cw - ctxt->width) / 2 : 0;
            auto dest_y = (ch > ctxt->height) ? (ch - ctxt->height) / 2 : 0;

            BitBlt(
                hdc,
                dest_x,
                dest_y,
                visible_w,
                visible_h,
                memDC,
                ctxt->pan_x,
                ctxt->pan_y,
                SRCCOPY
                );
            SelectObject(memDC, oldBmp);
            DeleteDC(memDC);

            RECT r_in, r_out, r_fit;
            get_toolbar_rects(hwnd, r_in, r_out, r_fit);
            draw_toolbar_button(hdc, r_in, ICON_ZOOM_IN);
            draw_toolbar_button(hdc, r_out, ICON_ZOOM_OUT);
            draw_toolbar_button(hdc, r_fit, ICON_FIT);
        }
        else
        {
            DrawText(
                hdc,
                L"SVG unavailable",
                -1,
                &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE
                );
        }
        EndPaint(hwnd, &ps);
        return 0;

    case WM_ERASEBKGND:
        return 1;  // handled in WM_PAINT

    case WM_SETCURSOR:
        if (ctxt && LOWORD(lp) == HTCLIENT)
        {
            if (ctxt->panning)
            {
                SetCursor(closed_hand_cursor());
            }
            else if (ctxt->bitmap
                && (ctxt->width > ctxt->client_cx || ctxt->height > ctxt->client_cy))
            {
                SetCursor(open_hand_cursor());
            }
            else
            {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            }
            return TRUE;
        }
        break;

    case WM_SIZE:
        if (ctxt)
        {
            render_for_client(ctxt, hwnd);
            InvalidateRect(hwnd, nullptr, false);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (ctxt && ctxt->bitmap)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);  // WM_MOUSEWHEEL coords are in screen space

            ctxt->wheel_accum += GET_WHEEL_DELTA_WPARAM(wp);
            if (abs(ctxt->wheel_accum) < WHEEL_DELTA)
            {
                return 0;
            }
            auto steps = ctxt->wheel_accum / WHEEL_DELTA;
            ctxt->wheel_accum %= WHEEL_DELTA;

            zoom_by(ctxt, hwnd, steps, pt);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (ctxt && ctxt->bitmap)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (try_toolbar_click(ctxt, hwnd, pt))
            {
                return 0;
            }

            SetFocus(hwnd);
            ctxt->panning = true;
            ctxt->pan_anchor.x = GET_X_LPARAM(lp);
            ctxt->pan_anchor.y = GET_Y_LPARAM(lp);
            ctxt->pan_anchor_x = ctxt->pan_x;
            ctxt->pan_anchor_y = ctxt->pan_y;
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (ctxt)
        {
            ctxt->last_mouse.x = GET_X_LPARAM(lp);
            ctxt->last_mouse.y = GET_Y_LPARAM(lp);
            if (ctxt->panning)
            {
                GetClientRect(hwnd, &rc);
                auto dx = ctxt->last_mouse.x - ctxt->pan_anchor.x;
                auto dy = ctxt->last_mouse.y - ctxt->pan_anchor.y;
                ctxt->pan_x = ctxt->pan_anchor_x - dx;
                ctxt->pan_y = ctxt->pan_anchor_y - dy;
                clamp_pan(ctxt, rc.right - rc.left, rc.bottom - rc.top);
                InvalidateRect(hwnd, nullptr, false);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (ctxt && ctxt->panning)
        {
            ctxt->panning = false;
            ReleaseCapture();
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (ctxt && ctxt->bitmap)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (!try_toolbar_click(ctxt, hwnd, pt))
            {
                reset_to_fit(ctxt, hwnd);
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (ctxt && (wp == 'F' || wp == 'f'))
        {
            reset_to_fit(ctxt, hwnd);
            return 0;
        }
        if (ctxt
            && (wp == VK_OEM_PLUS || wp == VK_ADD || wp == VK_OEM_MINUS || wp == VK_SUBTRACT))
        {
            auto steps = (wp == VK_OEM_PLUS || wp == VK_ADD) ? 1 : -1;
            zoom_by(ctxt, hwnd, steps, ctxt->last_mouse);
            return 0;
        }
        break;

    case WM_GETDLGCODE:
        // Total Commander's Lister frame runs its own dialog-style message
        // loop; without this, WM_KEYDOWN for F/+/-/arrows never reaches this
        // window at all - the frame consumes them as dialog navigation before
        // wnd_proc ever sees them. GPXLister.cpp (a sibling WLX plugin,
        // read-only reference) does the same.
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;

    case WM_DESTROY:
        if (ctxt)
        {
            if (ctxt->bitmap)
            {
                DeleteObject(ctxt->bitmap);
            }
            free(ctxt);
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API void WINAPI ListGetDetectString(PSTR detect_str, int maxlen)
{
    if (maxlen <= 0)
    {
        return;
    }
    static const CHAR ds[] = "EXT=\"SVG\" | EXT=\"SVGZ\"";
    strncpy(detect_str, ds, maxlen - 1);
    detect_str[maxlen - 1] = '\0';
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API HBITMAP WINAPI ListGetPreviewBitmapW(
    PCWSTR fname,
    int width,
    int height,
    BYTE*,
    int
    )
{
    LONG dummy_w, dummy_h;
    return bitmap_from_svg(fname, width, height, BG_COLOR, dummy_w, dummy_h);
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API HWND WINAPI ListLoadW(HWND parent, PCWSTR fname, int)
{
    auto* ctxt = static_cast<SvgCtxt*>(malloc(sizeof(SvgCtxt)));
    if (!ctxt)
    {
        return nullptr;
    }

    if (wnd_class == 0)
    {
        WNDCLASS wc{};
        wc.lpfnWndProc    = wnd_proc;
        wc.hInstance      = hinst();
        wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground  = HBRUSH(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName  = L"svg_wlx";
        wc.style          = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wnd_class = RegisterClass(&wc);
        if (wnd_class == 0)
        {
            free(ctxt);
            return nullptr;
        }
    }

    RECT rc;
    GetClientRect(parent, &rc);
    auto width = rc.right - rc.left;
    auto height = rc.bottom - rc.top;
    auto hwnd = CreateWindow(
        MAKEINTRESOURCE(wnd_class),
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        width,
        height,
        parent,
        nullptr,
        hinst(),
        nullptr
        );
    if (hwnd)
    {
        ctxt->bitmap = nullptr;
        ctxt->client_cx = 0;
        ctxt->client_cy = 0;
        ctxt->zoom = 1.0f;
        ctxt->pan_x = 0;
        ctxt->pan_y = 0;
        ctxt->wheel_accum = 0;
        ctxt->panning = false;
        ctxt->last_mouse.x = width / 2;
        ctxt->last_mouse.y = height / 2;
        wcsncpy_s(ctxt->fname, MAX_PATH, fname, _TRUNCATE);
        SetWindowLongPtr(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(ctxt)
            );
        render_for_client(ctxt, hwnd, true);
        // Without this, the window can sit unfocused until the user clicks
        // it, and WM_GETDLGCODE alone does not help until focus is actually
        // here - matches the pattern used by GPXLister.cpp (a sibling WLX
        // plugin, read-only reference), which sets focus right after create.
        SetFocus(hwnd);
        notify_fit_state(hwnd, true);
    }
    else
    {
        free(ctxt);
    }
    return hwnd;
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API HWND WINAPI ListLoad(HWND parent, PSTR fname, int flags)
{
    int wchars = MultiByteToWideChar(CP_ACP, 0, fname, -1, nullptr, 0);
    if (wchars <= 0)
    {
        return nullptr;
    }
    auto* wfname = static_cast<PWSTR>(malloc(wchars * sizeof(WCHAR)));
    if (!wfname)
    {
        return nullptr;
    }
    MultiByteToWideChar(CP_ACP, 0, fname, -1, wfname, wchars);
    auto hwnd = ListLoadW(parent, wfname, flags);
    free(wfname);
    return hwnd;
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API void WINAPI ListCloseWindow(HWND list_wnd)
{
    DestroyWindow(list_wnd);
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API int WINAPI ListLoadNextW(HWND, HWND svg_wnd, PCWSTR fname, int)
{
    auto* ctxt = get_ctxt(svg_wnd);
    if (!ctxt)
    {
        return LISTPLUGIN_ERROR;
    }

    wcsncpy_s(ctxt->fname, MAX_PATH, fname, _TRUNCATE);
    ctxt->zoom = 1.0f;  // new file: reset zoom/pan to fit-to-window
    ctxt->pan_x = 0;
    ctxt->pan_y = 0;
    render_for_client(ctxt, svg_wnd, true);
    InvalidateRect(svg_wnd, nullptr, true);
    notify_fit_state(svg_wnd, true);
    return ctxt->bitmap ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API int WINAPI ListLoadNext(HWND parent, HWND svg_wnd, PSTR fname, int flags)
{
    int wchars = MultiByteToWideChar(CP_ACP, 0, fname, -1, nullptr, 0);
    if (wchars <= 0)
    {
        return LISTPLUGIN_ERROR;
    }
    auto* wfname = static_cast<PWSTR>(malloc(wchars * sizeof(WCHAR)));
    if (!wfname)
    {
        return LISTPLUGIN_ERROR;
    }
    MultiByteToWideChar(CP_ACP, 0, fname, -1, wfname, wchars);
    auto result = ListLoadNextW(parent, svg_wnd, wfname, flags);
    free(wfname);
    return result;
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API int WINAPI ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    // Total Commander's own Lister frame owns the F key / "Fit image to
    // window" menu checkbox for image viewers - it is never delivered to us
    // as a raw WM_KEYDOWN. Instead Lister toggles its own menu state and
    // tells the plugin via lc_newparams/lcp_fittowindow here (see the WLX
    // SDK's listsendcommand.htm). Our own WM_KEYDOWN 'F' handler in wnd_proc
    // only fires in hosts that don't intercept the key this way (e.g. the
    // test_host harness), so both paths must call the same fit logic.
    if (Command == lc_newparams)
    {
        auto* ctxt = get_ctxt(ListWin);
        if (ctxt && (Parameter & lcp_fittowindow))
        {
            reset_to_fit(ctxt, ListWin);
        }
    }
    return LISTPLUGIN_OK;
}

////////////////////////////////////////////////////////////////////////////////

PLUGIN_API void WINAPI ListSetDefaultParams(ListDefaultParamStruct*)
{
}

////////////////////////////////////////////////////////////////////////////////
