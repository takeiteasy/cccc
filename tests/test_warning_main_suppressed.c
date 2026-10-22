// CCCC_NATIVE_SKIP: a void main's exit status is host-defined (native.md divergences)
// CCCC_FLAGS: -Wno-main
// CCCC_REJECT_STDERR: return type of 'main'
// CCCC_EXPECT_STDOUT: done

#include <stdio.h>

void main(void) {
    printf("done\n");
}
