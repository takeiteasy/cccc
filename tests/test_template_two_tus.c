// CCCC_FLAGS: tests/fixtures/template_tu_b.c
// Two translation units instantiate the same template; the function instance
// has internal linkage, so each keeps its own copy and the link succeeds.
#include "fixtures/template_vec.h"

int tu_b_value(void);

int main(void) {
    return @inst(int) max(38, 1) + tu_b_value(); // 38 + 4
}
