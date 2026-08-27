// CCCC_FLAGS: -O2
//
// #1159: -O<n> used to be a hard error under -c=native ("cannot be combined
// with VM bytecode options") -- opt_level tuned the VM's own bytecode
// pipeline, which the native path never runs, so the CLI simply refused the
// combination rather than doing something with it. run_native_backend() now
// forwards -O<n> verbatim to the host cc instead: this file's own `-O2`
// drives both the VM bytecode optimizer (the normal, unrelated meaning) and,
// under -c=native, the host compiler's own optimization level. Regression
// coverage for the CLI accepting `-O2 -c=native` at all, not for any
// optimization-quality difference (see man/COVERAGE.md's tail-call-
// elimination divergence row for a case that actually depends on the level).
int add(int a, int b) {
    return a + b;
}

int main(void) {
    return add(19, 23);
}
