#pragma once
#if CROSSINK_SCALABLE_FONTS
class GfxRenderer;
void ensureScalableBuiltinFamily(GfxRenderer& renderer, unsigned family);
int scalableBuiltinFontId(int legacyId);
int scalableBuiltinReaderFontId(unsigned family, unsigned points);
#endif
