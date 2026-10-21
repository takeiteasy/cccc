#define DECORATE_1308_SMOKE(name) v1308_smoke_##name
#define VENDORED_1308_SMOKE_IMPLEMENTATION
#include "vendored_1308_smoke_lib.h"
int vendored_macro_outside_1308_smoke_use(void) {
    return v1308_smoke_add(41);
}
