// A template defined in a header instantiates in the including file.
#include "fixtures/template_vec.h"

int main(void) {
    @inst(int) Vec v = {0};
    int x = 2;
    v.data = &x;
    v.len  = 1;
    return @inst(int) max(40, 42) - 42 + v.data[0] * 20 + 2;
}
