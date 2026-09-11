// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: build succeeded
// CCCC_REJECT_STDOUT: main\(\) ran
//
// #1272: a --build script MAY now define main() alongside its
// [[cccc::build]] entry -- main() is simply ignored, not rejected. Proven
// by giving main() an observable side effect (a stdout marker) and a
// nonzero return: if cc_run_build() ever invoked it, either the marker
// would show up (CCCC_REJECT_STDOUT catches it) or the nonzero exit would
// turn this from a "passed" build into a "failed" one.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "no_main_demo");
    AddSourceStr(t, "no_main_demo.c", "int main(void) { return 0; }\n");
    return BuildAll(ctx);
}

int main(void) {
    printf("main() ran\n");
    return 1;
}
