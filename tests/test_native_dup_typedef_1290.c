// CCCC_FLAGS: tests/fixtures/dup_typedef_1290_smoke_a.c tests/fixtures/dup_typedef_1290_smoke_b.c
// Two TUs defining the same typedef build under -c=native.
#include "fixtures/dup_typedef_1290_smoke.h"
extern int dup_typedef_1290_smoke_sum(DupTypedef1290Smoke v);
extern int dup_typedef_1290_smoke_make_and_use(void);
int main(void) {
    DupTypedef1290Smoke v = {21, 21};
    if (dup_typedef_1290_smoke_sum(v) != 42)
        return 1;
    if (dup_typedef_1290_smoke_make_and_use() != 42)
        return 1;
    return 42;
}
