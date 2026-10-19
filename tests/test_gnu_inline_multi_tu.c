// CCCC_FLAGS: tests/fixtures/gnu_inline_other_tu.c
// Expected return: 42
// The same gnu_inline definitions reached from two translation units are not a
// redefinition, and a body used in only one of them still links.
extern int undefined_shared_callee(int);
#include "fixtures/gnu_inline_shared.h"
extern int other_tu_value(void);

int main(void) {
    return other_tu_value() + shared_twice(1) == 42 ? 42 : 1;
}
