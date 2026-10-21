// CCCC_FLAGS: tests/fixtures/typedef_shape_1293_a.c tests/fixtures/typedef_shape_1293_b.c
// Two TUs defining a same-named typedef with different shapes build under
// -c=native.
#include "fixtures/typedef_shape_1293.h"
int typedef_shape_1293_local_use(void);
int main(void) {
    TypedefShape1293 s = typedef_shape_1293_hdr_make();
    int              r = s.a + s.b + typedef_shape_1293_local_use();
    return r == 45 ? 42 : 1;
}
