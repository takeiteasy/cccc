// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected unique generated function name, got existing definition 'P_equals'

#include <cccc/derive.h>

@hashable @ordered struct P {
    int a;
};

int main(void) {
    return 42;
}
