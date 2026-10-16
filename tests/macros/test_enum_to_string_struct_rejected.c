// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @enum_to_string expects an enum target

#include <cccc/derive.h>

@enum_to_string typedef struct {
    int x;
} NotAnEnum;

int main(void) {
    return 42;
}
