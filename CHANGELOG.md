# Changelog

## v2.1.2 - Transparency, animated-SVG handling, and a build-tooling fix

* Added: transparent SVG backgrounds now render as a light/dark checkerboard in the interactive Lister view, the usual image-editor convention for showing transparency (matches other viewers instead of always flattening to solid white).
* Added: `svg_wlx.ini` (optional, same folder/base name as the plugin binary) with `Checkerboard=1|0` and `TransparentBackgroundColor=RRGGBB` (used only when `Checkerboard=0`) - see `DIST_README.md` for details. Total Commander's file-list thumbnail preview is unaffected either way; it always flattens to solid white.
* Added: SVGs using SMIL (`<animate>`, `<set>`, etc.) or CSS (`@keyframes`, `animation:`) animation now show a small "Animated SVG - showing first frame only" notice in a strip reserved at the bottom of the Lister view, so it's clear the static image isn't the whole story. The notice never overlaps the picture - the image itself renders, zooms, and pans inside a correspondingly shorter area rather than being overlaid.
* Fixed: an SVG file starting with a UTF-8 BOM failed to render at all ("SVG unavailable") - the BOM made the underlying LunaSVG parser reject the entire document, not just the offending bytes, even though the rest of the file was well-formed.
* Fixed: `svg_wlx.sln`/`svg_wlx.vcxproj` hardcoded a `v145` platform toolset that doesn't exist on newer Visual Studio installs (only `v150`/`v160`/`v170`/`v180` do), which could hang Visual Studio 2026 preview while it tried to resolve/retarget the project on open. Now uses `$(DefaultPlatformToolset)`, which resolves to whatever toolset the running VS install actually has.

## v2.1.1 - Lister integration fixes

* Fixed: pressing **F** to fit the image to the window sometimes required two presses. Total Commander's own "Fit image to window" Lister menu checkbox is now kept in sync with the plugin's actual zoom state, so a single press always works.
* Fixed: rapidly clicking the on-screen zoom in/out buttons could snap thezoom back to 100% instead of continuing to zoom in the same direction.
* Changed: the pan cursor (shown while dragging a zoomed-in image) now uses real open-hand/closed-fist cursor artwork bundled with the plugin.

## v2.1.0 - Zoom, pan, and hardening

* Added: interactive zoom via mouse wheel or **+**/**-** (10%-800% of fit-to-window), centered on the mouse cursor.
* Added: click-and-drag panning once the image is zoomed in beyond the window size.
* Added: on-screen zoom in / zoom out / fit toolbar buttons.
* Added: **F** key or double-click resets zoom/pan to fit-to-window.
* Changed: replaced NanoSVG with LunaSVG as the rendering backend, adding support for text, `<use>`/`<symbol>`, `<clipPath>`, `<mask>`, `<pattern>`, and CSS styling (see `DIST_README.md` for details).
* Fixed: the rendered image blurring on Lister pane resize or DPI change; it now re-rasterizes at the new size instead of stretching the old bitmap.
* Fixed: several issues found in an internal security review (a buffer bound in `ListGetDetectString`, a file-read size mismatch, unbounded render dimensions on extreme zoom, and a leak on two rare window-creation failure paths).
