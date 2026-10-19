// CCCC_NATIVE_SKIP: a void main's exit status is host-defined (NATIVE.md divergences)
// CCCC_FLAGS: -Wmain
// CCCC_EXPECT_STDERR: return type of 'main' is not 'int'.*\[-Wmain\]
// CCCC_EXPECT_STDOUT: done

#include <stdio.h>

void main(void) {
    printf("done\n");
}
