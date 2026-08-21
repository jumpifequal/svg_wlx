// Minimal interactive WLX test harness: creates a real top-level window,
// loads a plugin's ListLoadW child window into it, and runs a normal
// message loop - so mouse wheel zoom, click-drag pan, and +/- keys can be
// tested by hand without installing the plugin into Total Commander.
//
// Build (from a VS "x64 Native Tools" / "x86 Native Tools" command prompt):
//   cl /EHsc /nologo interactive_host.cpp gdi32.lib user32.lib /Fe:interactive_host.exe
//
// Usage:
//   interactive_host.exe <plugin.wlx|wlx64> <input.svg>

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <cstdio>

typedef HWND (WINAPI *ListLoadW_t)(HWND, PCWSTR, int);
typedef void (WINAPI *ListCloseWindow_t)(HWND);

static HWND g_child = nullptr;
static ListCloseWindow_t g_ListCloseWindow = nullptr;

static LRESULT CALLBACK top_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_child)
        {
            MoveWindow(g_child, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        }
        return 0;
    case WM_DESTROY:
        if (g_child && g_ListCloseWindow)
        {
            g_ListCloseWindow(g_child);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3)
    {
        wprintf(L"usage: interactive_host <plugin.wlx> <input.svg>\n");
        return 1;
    }

    HMODULE mod = LoadLibraryW(argv[1]);
    if (!mod)
    {
        wprintf(L"LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    auto ListLoadW_fn = (ListLoadW_t)GetProcAddress(mod, "ListLoadW");
    g_ListCloseWindow = (ListCloseWindow_t)GetProcAddress(mod, "ListCloseWindow");
    if (!ListLoadW_fn || !g_ListCloseWindow)
    {
        wprintf(L"GetProcAddress failed: %lu\n", GetLastError());
        return 1;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = top_wnd_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = L"svg_wlx_interactive_host";
    RegisterClass(&wc);

    HWND top = CreateWindow(
        L"svg_wlx_interactive_host",
        L"svg_wlx interactive test - wheel/+- zoom, drag pan",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );
    if (!top)
    {
        wprintf(L"CreateWindow failed: %lu\n", GetLastError());
        return 1;
    }
    ShowWindow(top, SW_SHOW);

    g_child = ListLoadW_fn(top, argv[2], 0);
    if (!g_child)
    {
        wprintf(L"ListLoadW returned NULL (file not loaded)\n");
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    FreeLibrary(mod);
    return 0;
}
