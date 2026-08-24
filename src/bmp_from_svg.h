#pragma once

////////////////////////////////////////////////////////////////////////////////

HBITMAP bitmap_from_svg(
    PCWSTR fname,
    LONG max_width,
    LONG max_height,
    COLORREF back_color,
    LONG& width,
    LONG& height,
    bool checkerboard = false
    );

// True if the file's raw markup uses any SMIL or CSS animation construct
// (LunaSVG has no animation support and silently renders a static first
// frame regardless - this is only for deciding whether to say so).
bool svg_file_has_animation(PCWSTR fname);

////////////////////////////////////////////////////////////////////////////////
