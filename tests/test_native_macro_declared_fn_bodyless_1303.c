// A body-less function declared through a macro round-trips under -c=native.
#include "fixtures/macro_declared_fn_1303_smoke_lib.h"
int G1303_SMOKE_MK_NAME(s)(int);
int main(void) {
    return G1303_SMOKE_MK_NAME(s)(-42) == 42 ? 42 : 1;
}
