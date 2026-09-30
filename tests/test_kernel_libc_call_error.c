// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: calling 'malloc'
#include <stdlib.h>
[[cccc::kernel]] static void k(void) { free(malloc(4)); }
int main(void) { k(); return 42; }
