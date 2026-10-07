# Checking a font subset

Extract the eight original TTFs from the revision before subsetting into a temporary directory. Configure the native suite with `cmake -S test -B <build-dir> -G 'Unix Makefiles'`, then build the `scalable_font_subset_compare` target.

Run `<build-dir>/scalable_fonts/scalable_font_subset_compare <original-font-directory> lib/EpdFont/scalableFonts`.

The comparison uses the actual SDK renderer, checks BMP Unicode coverage and metrics, compares glyph pixels at three sizes in Default, Auto, and None hinting modes, and checks ASCII-pair kerning and standard Latin ligatures. Address/undefined-behavior sanitizers are enabled by default. It does not establish hardware memory headroom or screen refresh behavior.

Regenerate with `python scripts/subset_scalable_fonts.py` using fontTools 4.62.1. Keep the original inputs for this comparison. No reader cache format changes are involved.
