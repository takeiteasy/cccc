// CCCC_FLAGS: -Wold-style-definition -Wno-old-style-definition
// CCCC_EXPECT_STDOUT: done

#include <stdio.h>

int add(a, b)
int a;
int b;
{ return a + b; }

int main(void) {
    (void)add(1, 2);
    printf("done\n");
    return 42;
}
