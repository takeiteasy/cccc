// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: -framework CoreFoundation
//
// AddFramework: macOS -framework Name shorthand verified via dry-run output.
// Adds two linker tokens (-framework, <name>) so no cross-platform issue
// arises.

#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "fw_test");
    AddSourceStr(t, "fw_test.c", "int main(void) { return 0; }\n");
    AddFramework(t, "CoreFoundation");
    return BuildDefault(ctx);
}
