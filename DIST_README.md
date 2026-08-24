svg_wlx - SVG Lister Plugin for Total Commander
================================================

svg_wlx is a lister (WLX) plugin for [Total Commander](https://www.ghisler.com/index.htm)
that renders SVG files inline in the Lister/Quick View panel and as thumbnail
previews.

This build is a **fork of the original svg_wlx plugin by Rocco Matano**
(https://github.com/RoccoMatano/svg_wlx), with the rendering backend
replaced to add much broader SVG compatibility. See "What's changed in
this fork" below for details.


Contents of this package
-------------------------

| File                          | Purpose                                          |
|--------------------------------|---------------------------------------------------|
| `svg_wlx.wlx`                     | 32-bit plugin binary (for 32-bit Total Commander) |
| `svg_wlx.wlx64`                   | 64-bit plugin binary (for 64-bit Total Commander) |
| `svg_wlx.ini`                  | Optional settings file (see "Configuration" below)|
| `pluginst.inf`                | Install descriptor (enables drag-and-drop install)|
| `README.md`                   | Project README                                    |
| `LICENSE`                     | svg_wlx license (MIT)                             |
| `THIRD_PARTY_NOTICES.md`      | Licenses of statically-linked third-party code    |

If a diagram's text is too small to read at the current window size, use
the built-in zoom (mouse wheel or +/-, drag to pan) instead; see "Zoom and
pan" below.


Configuration
-------------

`svg_wlx.ini` is optional - if it's missing, or a setting in it is missing,
the plugin uses the defaults below. It must sit next to `svg_wlx.wlx` /
`svg_wlx.wlx64` and share their base name. Only the interactive Lister
preview is affected; Total Commander's file-list thumbnail preview always
flattens transparent SVGs onto solid white.

| Key                          | Default   | Meaning                                                              |
|-------------------------------|-----------|------------------------------------------------------------------------|
| `Checkerboard`                 | `1`       | `1` shows a light/dark checkerboard behind transparent pixels (the usual image-editor convention); `0` fills them with `TransparentBackgroundColor` instead. |
| `TransparentBackgroundColor`   | `FFFFFF`  | Hex RGB (no `#`) used to fill transparent pixels when `Checkerboard=0`. Ignored otherwise. |


Installation
------------

**Drag-and-drop (recommended):** drag this `.zip` onto the Total Commander
window and confirm the plugin install prompt. Total Commander will pick
`svg_wlx.wlx` or `svg_wlx.wlx64` automatically based on whether you're running the
32-bit or 64-bit build of Total Commander.

**Manual:** copy `svg_wlx.wlx`/`svg_wlx.wlx64` into a folder (e.g.
`%COMMANDER_PATH%\Plugins\wlx\svg_wlx\`), then in Total Commander go to
*Configuration -> Options -> Plugins -> Lister (WLX)* and add the plugin,
pointing at the appropriate binary for your Total Commander bitness.

No other files or runtime dependencies are required — both binaries are
fully self-contained (statically linked; verified with `dumpbin
/DEPENDENTS` to depend only on `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`).


Zoom and pan
------------

- **Mouse wheel** or **+ / -** zooms in/out, centered on the mouse
  cursor (or, for the keyboard shortcut, on the last known cursor
  position). Range is 10%-800% of fit-to-window.
- **Left-click and drag** pans around the image once it's zoomed in
  beyond the window size.
- **On-screen buttons** (top-right corner: zoom in, zoom out, fit) give
  the same controls without a keyboard or wheel.
- **F key** or **double-click** resets zoom/pan to fit-to-window.
- The mouse cursor becomes an **open hand** while hovering over a
  zoomed-in image (signaling it can be dragged) and a **closed hand**
  while actively dragging it — the typical pan-cursor convention.
- Zoom and pan also reset to fit-to-window whenever a new file is loaded.


What's changed in this fork
----------------------------

The original plugin rendered SVGs using [NanoSVG](https://github.com/memononen/nanosvg),
a minimal single-header parser that only supports basic shapes, gradients,
strokes/dashes, and opacity. It had no support for text, clipping, masking,
patterns, `<use>`/`<symbol>` reuse, or CSS styling — meaning a large share
of real-world SVGs (icon sets with text labels, diagrams, illustrations
using clip/mask/pattern) rendered incorrectly or with content silently
missing.

This fork replaces NanoSVG with [LunaSVG](https://github.com/sammycage/lunasvg)
(and its bundled [PlutoVG](https://github.com/sammycage/plutovg) rasterizer),
both statically linked in — no new runtime dependency, no external DLL, no
subprocess. This adds support for:

- **Text** (`<text>`, `<tspan>`) — rendered with real system fonts
- **`<use>` / `<symbol>`** — element reuse/instancing
- **`<clipPath>`** — vector clipping regions
- **`<mask>`** — alpha/luminance masking
- **`<pattern>`** — tiled pattern fills
- **`<style>` / CSS classes** — stylesheet-driven presentation, not just
  inline `style=` attributes
- **Embedded `<image>`** — raster images referenced from SVG

The one remaining gap is `<filter>` (`feGaussianBlur`, `feDropShadow`,
etc.) — not yet supported by LunaSVG upstream. This was already
unsupported before this fork (NanoSVG never had it either), so it is not a
regression, just a known limitation. SVGs using filters will still render
their base shapes/colors, just without the filter effect applied.

**Known pre-existing limitation, unchanged by this fork:** the plugin
advertises `.svgz` (gzip-compressed SVG) support via its file-type
detection string, but nothing in the code actually gzip-decompresses the
file before parsing — this was already the case before this fork. A
`.svgz` file will currently just fail to load ("SVG unavailable") rather
than render. Plain `.svg` files are unaffected.

**Known limitation: animated SVGs render as a static first frame.**
LunaSVG has no SMIL (`<animate>`, `<animateTransform>`, `<set>`, etc.) or
CSS (`@keyframes`, `animation:`) animation support - it silently ignores
those elements/properties rather than erroring on them, so the file still
renders, just frozen at its initial state. Building a real animation
engine (timing, easing, playback) was evaluated and deliberately not
pursued - out of proportion for a file-manager preview plugin. To avoid
that looking like the animation "just isn't playing" for no reason, the
plugin detects animation constructs in the file and shows a small
"Animated SVG - showing first frame only" notice in a reserved strip at
the bottom of the Lister view (it never overlaps the image itself).

**Fixed: a UTF-8 BOM at the start of an SVG file broke rendering
entirely.** Some SVG exporters emit one; LunaSVG's parser expects the
first byte to be `<`, so a leading BOM made it reject the whole document
("SVG unavailable") even though the rest of the file was perfectly valid
markup. The plugin now strips a leading BOM before handing the file to
the parser.

Two more fixes worth calling out from this fork's rewrite:

- **Fixed a "This is not a valid plugin!" install error.** Two related
  export-table issues:
  - The 32-bit build was exporting decorated stdcall names (e.g.
    `_ListLoadW@12`) instead of the plain `ListLoadW` Total Commander
    looks up, because MSVC's `__declspec(dllexport)` does not strip
    stdcall decoration on x86 by default. Fixed with a module-definition
    file (`src/svg_wlx.def`) that forces undecorated export names. (The
    64-bit build was unaffected by this specific issue - x64 has no name
    decoration.)
  - The plugin was only exporting the Unicode entry points (`ListLoadW`,
    `ListLoadNextW`), missing the ANSI `ListLoad`/`ListLoadNext` and the
    optional `ListSendCommand`/`ListSetDefaultParams` exports that Total
    Commander's plugin installer also expects to find - confirmed by
    diffing this plugin's export table against the known-working
    GPXLister plugin's. Added all four, delegating the ANSI entry points
    to their Unicode counterparts (same pattern GPXLister uses).
- **The rendered image now stays sharp when the Lister pane is resized**
  (including resizes triggered by moving the window to a differently
  DPI-scaled monitor). Previously the SVG was only rasterized once, on
  load, and any later resize just stretched that fixed bitmap with GDI's
  `StretchBlt`, blurring it. Since SVGs are vector data, the fix is to
  re-rasterize at the new size instead of stretching the old raster - the
  plugin now re-renders on `WM_SIZE`, so it is effectively resolution-
  independent at any DPI scale factor.

**New: interactive zoom and pan** (mouse wheel/+-/drag) — see "Zoom and
pan" above. Not present in the original upstream project. Renders always
stay crisp at any zoom level, since zooming re-rasterizes the vector SVG
at the new size rather than stretching a fixed bitmap.

**New: transparency rendered as a checkerboard, not flattened to white.**
Previously any transparent pixel in an SVG was always composited onto
solid white before display, so a logo meant for a transparent background
would just show white behind it. The interactive Lister view now shows
the usual image-editor checkerboard there instead (configurable via
`svg_wlx.ini` — see "Configuration" above); Total Commander's file-list
thumbnail preview is unchanged and still flattens to white.

**Hardening from an internal security review of the plugin's own code**
(the vendored LunaSVG/PlutoVG rendering library itself was explicitly out
of scope — its handling of malformed SVG content is its own responsibility):
a possible out-of-bounds write on a degenerate host-supplied buffer length
in `ListGetDetectString`; a file-size/actual-bytes-read mismatch in the
file-loading path that could let a file truncated mid-read be parsed with
uninitialized trailing bytes; a missing upper bound on rendered bitmap
dimensions that could let an extreme zoom level on a large SVG drive
memory allocation into the multi-gigabyte range; and a memory leak on two
rare window-creation failure paths. None of these had a demonstrated
real-world trigger during normal use — they were found and fixed
proactively.

Build-wise, the plugin also moved from a Python/SCons build script
(`bld.py`, which depended on a private build-helper module not published
alongside the project) to a plain MSVC solution/project
(`svg_wlx.sln`/`svg_wlx.vcxproj`), so it can be built with just Visual
Studio/MSBuild — no Python, no SCons, no Rust, nothing beyond a standard
C++ toolchain. The project file no longer pins a specific
`PlatformToolset` version (it previously hardcoded one, `v145`, that
doesn't exist on some Visual Studio installs and could hang the IDE on
project load); it now uses `$(DefaultPlatformToolset)`, which resolves to
whatever toolset the running Visual Studio install actually has.
