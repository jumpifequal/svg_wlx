![svg_wlx](src/svg.svg "SVG")
# svg_wlx

[![License - MIT](https://img.shields.io/badge/license-MIT-green)](https://spdx.org/licenses/MIT.html)

-----

svg_wlx is a [Total Commander](https://www.ghisler.com/index.htm) lister
plugin for viewing SVG files. This is a fork of the original
[svg_wlx by Rocco Matano](https://github.com/RoccoMatano/svg_wlx); see
[DIST_README.md](DIST_README.md) for what changed in this fork. It renders SVGs using
[LunaSVG](https://github.com/sammycage/lunasvg) (MIT license) and its
bundled [PlutoVG](https://github.com/sammycage/plutovg) rasterizer (MIT
license, includes FreeType-derived rasterizer/stroker code under the
FreeType License and vendored `stb_image`/`stb_truetype`), statically
linked in — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for
full license texts.
I wrote it because I wanted something similar to
[svgthumb](https://github.com/FrankEBailey/svgthumb), just a little different.
