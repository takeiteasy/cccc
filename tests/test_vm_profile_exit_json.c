// CCCC_FLAGS: --vm-profile --json
// CCCC_EXPECT_STDOUT: "total_opcodes"
// CCCC_NATIVE_SKIP: --vm-profile reports the VM run
// An explicit exit() still writes the JSON profile to stdout.
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc > 0)
        exit(42);
    return 1;
}
