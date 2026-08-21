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
| `pluginst.inf`                | Install descriptor (enables drag-and-drop install)|
| `README.md`                   | Project README                                    |
| `LICENSE`                     | svg_wlx license (MIT)                             |
| `THIRD_PARTY_NOTICES.md`      | Licenses of statically-linked third-party code    |

No `.ini` file is included, and none is needed: this plugin has no
user-configurable settings. Total Commander does not even call the
optional `ListSetDefaultParams` export here, and there is no code anywhere
in the plugin that reads an `.ini` file. Rendering behavior (scale-to-fit,
white letterbox background, 96 DPI) is fixed.


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

Build-wise, the plugin also moved from a Python/SCons build script
(`bld.py`, which depended on a private build-helper module not published
alongside the project) to a plain MSVC solution/project
(`svg_wlx.sln`/`svg_wlx.vcxproj`), so it can be built with just Visual
Studio/MSBuild — no Python, no SCons, no Rust, nothing beyond a standard
C++ toolchain.
