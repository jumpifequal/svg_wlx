// Minimal WLX test harness: loads a lister plugin DLL and calls
// ListGetPreviewBitmapW on a given file, saving the result as a 24-bit BMP.
// Not part of the plugin itself - a standalone dev tool for manual smoke
// testing without installing the plugin into Total Commander.
//
// Build (from a VS "x64 Native Tools" / "x86 Native Tools" command prompt):
//   cl /EHsc /nologo test_host.cpp gdi32.lib user32.lib /Fe:test_host.exe
//
// Usage:
//   test_host.exe <plugin.wlx|wlx64> <input.svg> <width> <height> <out.bmp>

#include <windows.h>
#include <cstdio>

typedef HBITMAP (WINAPI *ListGetPreviewBitmapW_t)(PCWSTR, int, int, BYTE*, int);

static bool save_bmp(HBITMAP bmp, const wchar_t* out)
{
    BITMAP bm;
    GetObject(bmp, sizeof(bm), &bm);
    LONG w = bm.bmWidth, h = bm.bmHeight;
    LONG stride = ((w * 3 + 3) & ~3);
    LONG imgSize = stride * h;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    BYTE* buf = (BYTE*)malloc(imgSize);
    HDC hdc = GetDC(nullptr);
    GetDIBits(hdc, bmp, 0, h, buf, &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);

    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = bmi.bmiHeader;
    ih.biHeight = -h;
    ih.biSizeImage = imgSize;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + imgSize;

    FILE* f = _wfopen(out, L"wb");
    if (!f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(buf, imgSize, 1, f);
    fclose(f);
    free(buf);
    return true;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 6)
    {
        wprintf(L"usage: test_host <plugin.wlx> <input.svg> <w> <h> <out.bmp>\n");
        return 1;
    }
    HMODULE mod = LoadLibraryW(argv[1]);
    if (!mod)
    {
        wprintf(L"LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    auto fn = (ListGetPreviewBitmapW_t)GetProcAddress(mod, "ListGetPreviewBitmapW");
    if (!fn)
    {
        wprintf(L"GetProcAddress failed: %lu\n", GetLastError());
        return 1;
    }
    int w = _wtoi(argv[3]);
    int h = _wtoi(argv[4]);
    HBITMAP bmp = fn(argv[2], w, h, nullptr, 0);
    if (!bmp)
    {
        wprintf(L"ListGetPreviewBitmapW returned NULL\n");
        return 1;
    }
    bool ok = save_bmp(bmp, argv[5]);
    wprintf(L"render %s -> %s : %s\n", argv[2], argv[5], ok ? L"OK" : L"FAIL");
    DeleteObject(bmp);
    FreeLibrary(mod);
    return 0;
}
