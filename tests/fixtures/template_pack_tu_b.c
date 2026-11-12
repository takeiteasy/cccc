#include "template_pack.h"

int tu_b_pack(void) {
    @inst(int, int) PackTuple t = {1, 2};
    return pack_sum @(int, int, int)(t.v0, t.v1, 4);
}
