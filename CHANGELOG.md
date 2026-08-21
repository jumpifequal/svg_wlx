# Changelog

## v2.1.1 - Lister integration fixes

* Fixed: pressing **F** to fit the image to the window sometimes required
  two presses. Total Commander's own "Fit image to window" Lister menu
  checkbox is now kept in sync with the plugin's actual zoom state, so a
  single press always works.
* Fixed: rapidly clicking the on-screen zoom in/out buttons could snap the
  zoom back to 100% instead of continuing to zoom in the same direction.
* Changed: the pan cursor (shown while dragging a zoomed-in image) now uses
  real open-hand/closed-fist cursor artwork bundled with the plugin.

## v2.1.0 - Zoom, pan, and hardening

* Added: interactive zoom via mouse wheel or **+**/**-** (10%-800% of
  fit-to-window), centered on the mouse cursor.
* Added: click-and-drag panning once the image is zoomed in beyond the
  window size.
* Added: on-screen zoom in / zoom out / fit toolbar buttons.
* Added: **F** key or double-click resets zoom/pan to fit-to-window.
* Changed: replaced NanoSVG with LunaSVG as the rendering backend, adding
  support for text, `<use>`/`<symbol>`, `<clipPath>`, `<mask>`, `<pattern>`,
  and CSS styling (see `DIST_README.md` for details).
* Fixed: "This is not a valid plugin!" install error caused by a missing
  module-definition file and incomplete export table.
* Fixed: the rendered image blurring on Lister pane resize or DPI change;
  it now re-rasterizes at the new size instead of stretching the old
  bitmap.
* Fixed: several issues found in an internal security review (a buffer
  bound in `ListGetDetectString`, a file-read size mismatch, unbounded
  render dimensions on extreme zoom, and a leak on two rare
  window-creation failure paths).
* Removed: the min-font-size/font-family `.ini` heuristic, superseded by
  interactive zoom.
