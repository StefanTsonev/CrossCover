#include "../../lib/ScalableFont/ScalableFontSizing.h"

#ifndef EXPECTED_PANEL_PPI
#error "EXPECTED_PANEL_PPI must be defined"
#endif

static_assert(ScalableFontCompatibilityPpi == EXPECTED_PANEL_PPI);
static_assert(scalableFontPixelSize26_6(12) == 1600);

int main() { return 0; }
