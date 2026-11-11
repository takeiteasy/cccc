#include "template_static.h"

int (*tu_b_twice(void))(int) {
    return &@inst(int) twice;
}
