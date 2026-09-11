// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: bt_demo
//
// #1272: a @build_target factory alongside main(), no [[cccc::build]] entry
// and no build_main -- covers both halves of the ticket at once: --build no
// longer rejects the file for defining main(), and with no entry present the
// sole factory is run automatically (entry-less fallback) rather than
// failing with "entry 'build_main' not found".

#include <stdio.h>

int main(void) {
    printf("hello\n");
    return 0;
}

@build_target BuildTarget *bt_demo(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "bt_demo");
    AddSourceStr(t, "bt_demo.c", "int main(void) { return 0; }\n");
    return t;
}
