// CCCC_FLAGS: tests/fixtures/include_spelling_1292_a.c
// A header included as "./x.h" by one TU and "x.h" by another is one header
// under -c=native.
#include "fixtures/include_spelling_1292.h"
int include_spelling_1292_use_a(void);
int main(void) {
    IncludeSpelling1292Pair p;
    p.a   = 20;
    p.b   = 19;
    int r = include_spelling_1292_use_a() + include_spelling_1292_sum(p);
    return r == 42 ? 42 : 1;
}
