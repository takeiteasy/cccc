#include "template_vec.h"

int tu_b_value(void) {
    @inst(int) Vec v = {0};
    int x = 3;
    v.data = &x;
    return @inst(int) max(v.data[0], 4);
}
