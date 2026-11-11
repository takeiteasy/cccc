// CCCC_FLAGS: tests/fixtures/template_static_tu_b.c
// A static template keeps one private instance per translation unit.
#include "fixtures/template_static.h"

int (*tu_b_twice(void))(int);

int main(void) {
    int (*mine)(int) = &@inst(int) twice;
    return tu_b_twice() != mine && mine(21) == 42 ? 42 : 1;
}
