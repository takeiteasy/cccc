// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: unknown attribute 'overloadable' ignored
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
