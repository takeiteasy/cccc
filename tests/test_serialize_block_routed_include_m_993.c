// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct Pt \{(.|\n)*struct __cccc_block_env_
// -m output places the struct definition before the block env struct.
#include "fixtures/routedlib_993.c"
int main(void) {
    struct Pt p;
    p.x            = 42;
    int (^b)(void) = ^{
      return p.x;
    };
    return b();
}
