// CCCC_EXPECT_STDOUT: ^done$
// Without --checked-pointers the libc bounds are not applied: this overrun of
// a (it stays inside the struct) runs to completion.
#include <stdio.h>
#include <string.h>

int main(void) {
    struct {
        char a[8];
        char b[16];
    } s               = {0};
    volatile size_t n = 16;
    memmove(s.a, s.b, n);
    puts("done");
    return 42;
}
