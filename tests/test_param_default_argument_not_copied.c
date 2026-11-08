// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: r1 = box\(10, 10 \* 2\);
// CCCC_EXPECT_STDOUT: r2 = box\(i, i \* 2\);
// CCCC_EXPECT_STDOUT: r3 = box\(7, 7 \* 2\);
// CCCC_EXPECT_STDOUT: r4 = \(__cccc_tmp[0-9]+ = v , box\(__cccc_tmp[0-9]+, __cccc_tmp[0-9]+ \* 2\)\);
// CCCC_EXPECT_STDOUT: r5 = \(__cccc_tmp[0-9]+ = \(i = i \+ 1\) \+ -1 , box\(
// CCCC_EXPECT_STDOUT: r6 = \(__cccc_tmp[0-9]+ = \(\*p = 5 , i\) , box\(
// A default that names an earlier parameter reads a literal or a plain local
// argument directly; a volatile local, or any argument in a call with side
// effects, goes through a temporary.

#include <cccc/derive.h>

enum { K = 7 };

int box(int w, int h @ default(w * 2)) {
    return w * 100 + h;
}

int *p;

int main(void) {
    int          i  = 3;
    volatile int v  = 4;
    int          r1 = box(10);
    int          r2 = box(i);
    int          r3 = box(K);
    int          r4 = box(v);
    int          r5 = box(i++);
    p               = &i;
    int r6          = box((*p = 5, i));
    return (r1 == 1020 && r2 == 306 && r3 == 714 && r4 == 408 && r5 == 306 &&
            r6 == 510)
               ? 42
               : 1;
}
