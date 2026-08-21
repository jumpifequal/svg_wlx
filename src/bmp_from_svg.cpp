#include "pch.h"
#include "bmp_from_svg.h"

#include "lunasvg.h"

////////////////////////////////////////////////////////////////////////////////

static char* read_file(PCWSTR fname, DWORD& size)
{
    char* buf = nullptr;
    size = 0;
    auto hdl = CreateFile(
        fname,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
        );
    if (hdl != INVALID_HANDLE_VALUE)
    {
        auto file_size = GetFileSize(hdl, nullptr);
        if (file_size != 0 && file_size != INVALID_FILE_SIZE)
        {
            buf = static_cast<char*>(malloc(file_size));
            if (buf)
            {
                if (ReadFile(hdl, buf, file_size, nullptr, nullptr))
                {
                    size = file_size;
                }
                else
                {
                    free(buf);
                    buf = nullptr;
                }
            }
        }
        CloseHandle(hdl);
    }
    return buf;
}

////////////////////////////////////////////////////////////////////////////////

HBITMAP bitmap_from_svg(
    PCWSTR fname,
    LONG max_width,
    LONG max_height,
    COLORREF back_color,
    LONG& width,
    LONG& height
    )
{
    DWORD size = 0;
    auto* buf = read_file(fname, size);
    if (!buf)
    {
        return nullptr;
    }

    auto doc = lunasvg::Document::loadFromData(buf, size);
    free(buf);
    if (!doc)
    {
        return nullptr;
    }

    auto doc_width = doc->width();
    auto doc_height = doc->height();
    if (doc_width <= 0.0f || doc_height <= 0.0f)
    {
        return nullptr;
    }

    auto scaleX = float(max_width) / doc_width;
    auto scaleY = float(max_height) / doc_height;
    auto scale = (scaleX < scaleY) ? scaleX : scaleY;
    auto w = LONG(doc_width * scale);
    auto h = LONG(doc_height * scale);
    width = w;
    height = h;

    auto bitmap = doc->renderToBitmap(w, h);
    if (bitmap.isNull())
    {
        return nullptr;
    }
    bitmap.convertToRGBA();  // un-premultiply, straight RGBA byte order

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;   // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    auto hdc = GetDC(nullptr);
    BYTE* bgr = nullptr;
    auto bmp = CreateDIBSection(
        hdc,
        &bmi,
        DIB_RGB_COLORS,
        reinterpret_cast<void**>(&bgr),
        nullptr,
        0);
    ReleaseDC(nullptr, hdc);

    const auto BG_R = GetRValue(back_color);
    const auto BG_G = GetGValue(back_color);
    const auto BG_B = GetBValue(back_color);
    const auto row_bytes = ((w * 3 + 3) & ~3);
    const auto src_stride = bitmap.stride();
    const auto* rgba = bitmap.data();
    if (bmp && bgr)
    {
        for (auto y = 0; y < h; y++)
        {
            auto rss = y * src_stride;
            auto rsd = y * row_bytes;
            for (auto x = 0; x < w; x++)
            {
                auto sidx = rss + x * 4;
                auto sr = rgba[sidx + 0];
                auto sg = rgba[sidx + 1];
                auto sb = rgba[sidx + 2];
                auto sa = rgba[sidx + 3];

                auto didx = rsd + x * 3;
                bgr[didx + 0] = (sb * sa + BG_B * (255 - sa)) / 255;
                bgr[didx + 1] = (sg * sa + BG_G * (255 - sa)) / 255;
                bgr[didx + 2] = (sr * sa + BG_R * (255 - sa)) / 255;
            }
        }
    }
    return bmp;
}

////////////////////////////////////////////////////////////////////////////////

BOOL save_svg_bitmap(PCWSTR fname, HBITMAP bmp)
{
    BITMAP bm;
    GetObject(bmp, sizeof(bm), &bm);
    if (bm.bmBits == nullptr)
    {
        return false;
    }

    LONG width  = bm.bmWidth;
    LONG height = bm.bmHeight;
    LONG stride = ((width * 3 + 3) & ~3); // DWORD aligned row size
    LONG imageSize = stride * height;

    BITMAPFILEHEADER fileHeader = {};
    BITMAPINFOHEADER infoHeader = {};

    infoHeader.biSize        = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
    infoHeader.biWidth       = width;
    infoHeader.biHeight      = -height;
    infoHeader.biPlanes      = 1;
    infoHeader.biBitCount    = 24;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage   = imageSize;

    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfOffBits = static_cast<DWORD>(
        sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
        );
    fileHeader.bfSize = fileHeader.bfOffBits + imageSize;

    HANDLE file = CreateFile(
        fname,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
        );
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    WriteFile(file, &fileHeader, sizeof(fileHeader), nullptr, nullptr);
    WriteFile(file, &infoHeader, sizeof(infoHeader), nullptr, nullptr);
    WriteFile(file, bm.bmBits, imageSize, nullptr, nullptr);
    CloseHandle(file);

    return true;
}

////////////////////////////////////////////////////////////////////////////////
