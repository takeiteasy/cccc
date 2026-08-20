// Regression test: a static constructor is reachable only through the
// attribute (no call site references it), so it must be marked live/root to
// survive DCE, and its code_addr must be kept in sync with the optimizer's
// text-compaction remap (both bugs were caught only under -O).
// CCCC_FLAGS: -O3

static int ran = 0;

static __attribute__((constructor)) void init(void) {
    ran = 1;
}

int main(void) {
    return ran ? 42 : 1;
}
