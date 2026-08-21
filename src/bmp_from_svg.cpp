#include "pch.h"
#include "bmp_from_svg.h"

#include "lunasvg.h"

////////////////////////////////////////////////////////////////////////////////

// Absolute ceiling on the requested render box, independent of zoom level or
// the SVG's own declared size. Without this, a large/adversarial SVG combined
// with the zoom feature's up-to-8x multiplier on a maximized window could
// drive renderToBitmap()/CreateDIBSection() toward multi-gigabyte allocations.
static const LONG MAX_RENDER_DIMENSION = 8192;

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
                DWORD bytes_read = 0;
                // Use the actual bytes transferred, not the pre-read file_size:
                // if the file is truncated by another process between GetFileSize
                // and here, trusting file_size would tell the caller (and the SVG
                // parser) that uninitialized tail bytes of buf are valid content.
                if (ReadFile(hdl, buf, file_size, &bytes_read, nullptr))
                {
                    size = bytes_read;
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

    if (max_width > MAX_RENDER_DIMENSION) max_width = MAX_RENDER_DIMENSION;
    if (max_height > MAX_RENDER_DIMENSION) max_height = MAX_RENDER_DIMENSION;

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
