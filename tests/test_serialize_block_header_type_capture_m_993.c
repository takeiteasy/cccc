// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include <time\.h>(.|\n)*struct __cccc_block_env_
// -m output places the #include ahead of the env struct that needs it.
#include <time.h>
int main(void) {
    struct tm t;
    t.tm_year      = 42;
    int (^b)(void) = ^{
      return t.tm_year;
    };
    return b();
}
