// Selects the active board's hardware definition based on a `BOARD_*` preprocessor define set in
// platformio.ini's build_flags for the environment being built. Application code should include
// this header (not any boards/board_*.h directly) and use blebridge::board::kWhatever - never
// hardcode a pin number or product name inline. See boards/board_template.h for the full list of
// what a board must define and how to add a new one.
#pragma once

#if defined(BOARD_NANOESP32_C6)
#include "boards/board_nanoesp32_c6.h"
#else
#error "No board selected - define a BOARD_* macro (e.g. BOARD_NANOESP32_C6) in platformio.ini's build_flags. See include/boards/board_template.h."
#endif
