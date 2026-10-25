// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @from_string expects an enum target

#include <cccc/derive.h>

@from_string typedef struct {
    int x;
} NotAnEnum;

int main(void) {
    return 42;
}
