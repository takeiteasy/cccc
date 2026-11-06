// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @default does not apply to a parameter of a @comptime function

#include <cccc/derive.h>

@comptime Node *pick(Node *a, Node *b @ default(2)) {
    return a;
}

int main(void) {
    return pick(42, 1);
}
