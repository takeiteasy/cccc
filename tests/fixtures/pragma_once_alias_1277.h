#pragma once

// #1277 regression fixture. A `#pragma once` header carrying a `static
// inline` helper. Reached from the test under two different path spellings
// in one TU (directly via -I, and via pragma_once_alias_1277_via.h's own
// `#include "./..."`). CCCC's `#pragma once` was keyed on the raw path
// string, so the second spelling slipped past it and this helper was
// redefined -- exactly the shape src/macros.c hits (`./internal.h` vs the
// `./parse_internal.h` chain's `src/././internal.h`).
static inline int pragma_once_alias_answer(void) {
    return 42;
}
