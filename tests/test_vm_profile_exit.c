// CCCC_FLAGS: --vm-profile
// CCCC_EXPECT_STDERR: VM opcode profile
// CCCC_NATIVE_SKIP: --vm-profile reports the VM run
// An explicit exit() still prints the profile report.
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc > 0)
        exit(42);
    return 1;
}
