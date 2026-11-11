// CCCC_FLAGS: tests/fixtures/template_spec_tu_b.c
// A specialization in a header applies in every file that includes it.
#include "fixtures/template_spec.h"

int spec_tu_b_value(void);

int main(void) {
    return @inst(int) twice(10) + spec_tu_b_value() +
           @inst(long) twice(3L); // 30 + 6 + 6
}
