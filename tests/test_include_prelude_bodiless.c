// CCCC_FLAGS: tests/fixtures/include_prelude_bodiless_def.c
// Expected return: 42
// A header body may call a function the input declares before the #include
// and defines in another translation unit, or never defines at all.
extern int forty(void);
extern int never_defined(void);
#include "fixtures/include_prelude_bodiless.h"

int main(void) {
    return plus_two();
}
