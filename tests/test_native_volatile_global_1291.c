// -c=native regression (#1291): a header-declared `volatile` global must
// keep its qualifier when -c=native re-spells the forward declaration (the
// #918 forward-declare-every-global pass) and the definition -- serialize_
// type() used to never emit is_volatile at all (only is_const), so a
// `static int g_vol_scalar = 0;` re-declaration collided with this file's
// own `extern volatile int g_vol_scalar;` ("redeclaration ... with a
// different type: 'int' vs 'volatile int'"). Covers both a scalar global
// and an array global (the array's element type carries is_volatile, not
// the array Type itself).
#include "native_volatile_global_1291.h"

volatile int g_vol_scalar     = 0;
volatile int g_vol_array[4]   = {0, 0, 0, 0};

int main(void) {
    g_vol_scalar = 40;
    g_vol_array[0] = 1;
    g_vol_array[1] = 1;
    return g_vol_scalar + g_vol_array[0] + g_vol_array[1];
}
