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

// Classic image-editor checkerboard: two neutral grays, 8x8 screen-pixel
// cells anchored at the bitmap's own (0,0) so it re-tiles cleanly at any
// render size (zoom re-renders the whole bitmap rather than stretching it).
static const int CHECKER_CELL = 8;
static const BYTE CHECKER_LIGHT = 0xFF;
static const BYTE CHECKER_DARK  = 0xC0;

inline BYTE checker_shade(LONG x, LONG y)
{
    return (((x / CHECKER_CELL) + (y / CHECKER_CELL)) & 1) ? CHECKER_DARK : CHECKER_LIGHT;
}

////////////////////////////////////////////////////////////////////////////////

// Case-insensitive substring search over a length-bounded, non-null-
// terminated buffer (the file content as read - not safe to hand to strstr).
static bool contains_ci(const char* data, DWORD len, const char* needle)
{
    auto nlen = strlen(needle);
    if (nlen == 0 || len < nlen)
    {
        return false;
    }
    for (DWORD i = 0; i + nlen <= len; i++)
    {
        size_t j = 0;
        for (; j < nlen; j++)
        {
            auto a = data[i + j];
            if (a >= 'A' && a <= 'Z') a = char(a + 32);
            if (a != needle[j])
            {
                break;
            }
        }
        if (j == nlen)
        {
            return true;
        }
    }
    return false;
}

// True if the raw markup uses any SMIL (<animate>/<animateTransform>/
// <animateMotion>/<animateColor>/<set>) or CSS (@keyframes, animation:/
// animation-name:) animation construct. LunaSVG has no animation support at
// all - it silently drops every element/property above rather than erroring
// on it - so the plugin renders a static first frame either way; this is
// only used to decide whether to tell the user that's what happened.
static bool svg_source_has_animation(const char* data, DWORD len)
{
    if (contains_ci(data, len, "<animate"))  // covers the whole animate* family
    {
        return true;
    }
    if (contains_ci(data, len, "@keyframes")
        || contains_ci(data, len, "animation:")
        || contains_ci(data, len, "animation-name"))
    {
        return true;
    }
    // "<set" alone would also match a hypothetical "<settings" tag, so
    // require a real tag boundary after it.
    static const char* SET = "<set";
    auto slen = strlen(SET);
    for (DWORD i = 0; i + slen <= len; i++)
    {
        size_t j = 0;
        for (; j < slen; j++)
        {
            auto a = data[i + j];
            if (a >= 'A' && a <= 'Z') a = char(a + 32);
            if (a != SET[j])
            {
                break;
            }
        }
        if (j == slen && i + slen < len)
        {
            auto next = data[i + slen];
            if (next == ' ' || next == '\t' || next == '\r' || next == '\n'
                || next == '>' || next == '/')
            {
                return true;
            }
        }
    }
    return false;
}

bool svg_file_has_animation(PCWSTR fname)
{
    DWORD size = 0;
    auto* buf = read_file(fname, size);
    if (!buf)
    {
        return false;
    }

    auto* content = buf;
    auto content_size = size;
    if (content_size >= 3
        && BYTE(content[0]) == 0xEF && BYTE(content[1]) == 0xBB && BYTE(content[2]) == 0xBF)
    {
        content += 3;
        content_size -= 3;
    }

    auto result = svg_source_has_animation(content, content_size);
    free(buf);
    return result;
}

////////////////////////////////////////////////////////////////////////////////

HBITMAP bitmap_from_svg(
    PCWSTR fname,
    LONG max_width,
    LONG max_height,
    COLORREF back_color,
    LONG& width,
    LONG& height,
    bool checkerboard,
    bool allow_upscale
    )
{
    DWORD size = 0;
    auto* buf = read_file(fname, size);
    if (!buf)
    {
        return nullptr;
    }

    // Skip a leading UTF-8 BOM (EF BB BF) - some SVG exporters emit one, and
    // LunaSVG's parser expects the very first byte to be '<', so a BOM left
    // in place makes it reject the entire document (every element, not just
    // the offending bytes) with no way to tell that from any other parse
    // failure.
    auto* content = buf;
    auto content_size = size;
    if (content_size >= 3
        && BYTE(content[0]) == 0xEF && BYTE(content[1]) == 0xBB && BYTE(content[2]) == 0xBF)
    {
        content += 3;
        content_size -= 3;
    }

    auto doc = lunasvg::Document::loadFromData(content, content_size);
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
    if (!allow_upscale && scale > 1.0f)
    {
        scale = 1.0f;
    }
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

                auto bg_r = BG_R;
                auto bg_g = BG_G;
                auto bg_b = BG_B;
                if (checkerboard && sa < 255)
                {
                    auto shade = checker_shade(x, y);
                    bg_r = bg_g = bg_b = shade;
                }

                auto didx = rsd + x * 3;
                bgr[didx + 0] = (sb * sa + bg_b * (255 - sa)) / 255;
                bgr[didx + 1] = (sg * sa + bg_g * (255 - sa)) / 255;
                bgr[didx + 2] = (sr * sa + bg_r * (255 - sa)) / 255;
            }
        }
    }
    return bmp;
}

////////////////////////////////////////////////////////////////////////////////
