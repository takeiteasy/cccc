// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include <stdio\.h>
// A plain #include <stdio.h> resolved from the embedded header table stays in
// -m output.
#include <stdio.h>
int main(void) {
    printf("hi\n");
    return 42;
}
