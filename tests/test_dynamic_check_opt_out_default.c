// #486: without --checked-pointers, dynamic_check() parses to a plain
// ND_NULL_EXPR (same convention as __builtin_assume) -- a false condition
// is never evaluated and never traps.

int main(void) {
    int i = 10, n = 5;
    __builtin_cccc_dynamic_check(i < n);
    return 42;
}
