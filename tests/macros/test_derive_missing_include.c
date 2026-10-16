// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined custom attribute 'serialize'.*#include <cccc/derive.h>

@serialize
struct Point {
    int x;
    int y;
};

int main(void) {
    return 42;
}
