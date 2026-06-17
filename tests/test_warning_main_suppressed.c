// CCCC_FLAGS: -Wno-main
// CCCC_REJECT_STDERR: return type of 'main'
// CCCC_EXPECT_STDOUT: done

#include <stdio.h>

void main(void) {
    printf("done\n");
}
