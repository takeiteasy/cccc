// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: calling 'puts'.*\(k -> mid -> leaf\)
#include <stdio.h>
static void leaf(void) { puts("x"); }
static void mid(void) { leaf(); }
[[cccc::kernel]] static void k(void) { mid(); }
int main(void) { k(); return 42; }
