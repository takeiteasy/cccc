// CCCC_FLAGS: --testing
// Consolidated suite: per-test stdout/stderr capture. A test with an
//   expect_stdout/expect_stderr pattern has its std fds redirected for the
//   span of the test; these cover the two ways that redirection used to
//   deadlock the runner.

#include <stdio.h>
#include <unistd.h>

// A test that leaks a descriptor onto the captured stream is the shape a
// sanitizer's symbolizer takes: it outlives the test body still holding the
// inherited fd. Reading the capture until end-of-file never finished.
[[cccc::test(return = 42, expect_stderr = "leaked-holder-marker")]]
int test_capture_survives_leaked_holder(void) {
    fprintf(stderr, "leaked-holder-marker\n");
    fflush(stderr);
    int held = dup(2);
    if (held < 0)
        return 1;
    return 42;
}

// More output than a pipe buffer holds. Nothing drains the capture until the
// body returns, so a pipe blocked the test in write() once it passed 64 KiB.
[[cccc::test(return = 42, expect_stdout = "overflow-tail-marker")]]
int test_capture_survives_overflow(void) {
    for (int i = 0; i < 200000; i++)
        putchar('x');
    printf("\noverflow-tail-marker\n");
    fflush(stdout);
    return 42;
}
