// Under --attr-target=c23, Clang attributes print in clang::, and those with
// no [[ ]] form stay __attribute__.
// CCCC_FLAGS: -E --attr-target=c23 --output-compiler=clang
// CCCC_EXPECT_STDOUT: \[\[clang::overloadable\]\] int f\(int\);
// CCCC_EXPECT_STDOUT: void s\(void \*const p \[\[clang::pass_object_size\(0\)\]\]\);
// CCCC_EXPECT_STDOUT: int h\(int n\) __attribute__\(\(enable_if\(n > 0, "p"\)\)\);
// CCCC_EXPECT_STDOUT: \[\[clang::unavailable\]\] void u\(void\);
// CCCC_REJECT_STDOUT: gnu::overloadable|gnu::enable_if
__attribute__((overloadable)) int f(int);
void s(void *const p __attribute__((pass_object_size(0))));
int h(int n) __attribute__((enable_if(n > 0, "p")));
__attribute__((unavailable)) void u(void);
int main(void) {
    return 42;
}
