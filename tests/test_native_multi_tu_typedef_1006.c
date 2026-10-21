// CCCC_FLAGS: tests/fixtures/multi_tu_1006_tu1.c
// Two TUs sharing typedefs and includes build under -c=native.
#include <stdlib.h>
#include <stdio.h>
typedef enum { MULTI_TU_1006_A, MULTI_TU_1006_B } MultiTu1006Thing;
static MultiTu1006Thing multi_tu_1006_pick(const char *s) {
    (void)s;
    return MULTI_TU_1006_B;
}
int multi_tu_1006_other(void);
int main(void) {
    MultiTu1006Thing t = multi_tu_1006_pick("x");
    void            *p = malloc(8);
    free(p);
    puts(t == MULTI_TU_1006_B ? "ok" : "no");
    return multi_tu_1006_other() + (t == MULTI_TU_1006_B ? 41 : 0);
}
