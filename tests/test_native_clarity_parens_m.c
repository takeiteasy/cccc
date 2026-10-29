// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: r = a & \(b != 0\);
// CCCC_EXPECT_STDOUT: s = \(a \+ b\) << 1;
// CCCC_EXPECT_STDOUT: t = a \|\| \(b && c\);
// CCCC_EXPECT_STDOUT: v = a \| \(b & c\);
// CCCC_EXPECT_STDOUT: = \(_Bool\)\(ok & \(id != 0\)\)
// -m: operand pairs gcc/clang's -Wparentheses flags are printed parenthesized,
// including a bool compound assignment's right-hand side.
#include <stdbool.h>

int main(void) {
    int  a = 1, b = 2, c = 0, id = 0;
    int  r   = a & (b != 0);
    int  s   = (a + b) << 1;
    int  t   = a || (b && c);
    int  v   = a | (b & c);
    bool ok  = true;
    ok      &= id != 0;
    return r + s + t + v + ok + 33;
}
