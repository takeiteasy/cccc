// A global declared through a macro round-trips under -c=native.
#include "fixtures/macro_declared_global_1303_smoke_lib.h"
G1303_SMOKE_DEFINE(thing)
int main(void) {
    return g1303_smoke_thing * 2;
}
