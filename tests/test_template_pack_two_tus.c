// CCCC_FLAGS: tests/fixtures/template_pack_tu_b.c
// Two translation units instantiate the same pack template; the function
// instance is shared, so the link succeeds.
#include "fixtures/template_pack.h"

int tu_b_pack(void);

int main(void) {
    return pack_sum @(int, int, int)(30, 1, 4) + tu_b_pack(); // 35 + 7
}
