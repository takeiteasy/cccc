// A template defined in a header instantiates in the including file.
#include "fixtures/template_vec.h"

int main(void) {
    @inst(Vec, int) v = {0};
    int x = 2;
    v.data = &x;
    v.len  = 1;
    return @inst(max, int)(40, 42) - 42 + v.data[0] * 20 + 2;
}
