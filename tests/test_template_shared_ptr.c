// CCCC_FLAGS: tests/fixtures/template_ptr_tu_b.c
// A function pointer to one instance is the same in every translation unit.
#include "fixtures/template_vec.h"

int (*tu_b_max(void))(int, int);

int main(void) {
    return tu_b_max() == &@inst(int) max ? 42 : 1;
}
