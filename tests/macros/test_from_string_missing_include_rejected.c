// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @from_string needs #include <cccc/text.h>

#include <cccc/derive.h>

@from_string struct Point {
    int x;
};

int main(void) {
    return 42;
}
