// Unscoped [[kernel]] is not the cccc attribute: it is ignored with a warning.
// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: unknown attribute .kernel. ignored

#include <stdlib.h>

[[kernel]] static void k(void) { free(NULL); }

int main(void) {
    k();
    return 42;
}
