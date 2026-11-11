#include "template_vec.h"

int (*tu_b_max(void))(int, int) {
    return &@inst(int) max;
}
