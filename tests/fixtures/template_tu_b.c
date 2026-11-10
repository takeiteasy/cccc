#include "template_vec.h"

int tu_b_value(void) {
    @inst(Vec, int) v = {0};
    int x = 3;
    v.data = &x;
    return @inst(max, int)(v.data[0], 4);
}
