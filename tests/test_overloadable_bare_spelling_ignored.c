// CCCC_EXPECT_STDERR: 'overloadable' is not a standard attribute; write \[\[clang::overloadable\]\]
// CCCC_EXPECT_STDERR: 'overloadable' is not a 'gnu' attribute; write \[\[clang::overloadable\]\]
// Only the GNU and `clang::` spellings mark a function overloadable.

[[overloadable]] int plain(int x) {
    return x;
}

[[gnu::overloadable]] int gnu_scoped(int x) {
    return x;
}

int main(void) {
    return plain(1) + gnu_scoped(41);
}
