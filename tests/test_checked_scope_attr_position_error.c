// EXPECT_COMPILE_ERROR
// Checked regions (#485): 'checked'/'unchecked' attach to a function
// definition or a compound statement -- not to a pointer's post-'*'
// qualifier position, where the six checked-*pointer* attributes live.
// apply_checked_scope_attr() (src/parse_types.c) rejects this position
// explicitly (attr == NULL there, since pointers() passes ty but not attr).

int main(void) {
    int x                    = 0;
    int *[[cccc::checked]] p = &x;
    (void)p;
    return 42;
}
