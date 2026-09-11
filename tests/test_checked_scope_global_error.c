// EXPECT_COMPILE_ERROR
// Checked regions (#485): an unchecked pointer global declared inside a
// #pragma cccc checked begin/end region is a compile error -- this exercises
// the pragma-stamped (not attribute-introduced) region, and
// global_variable() (src/parse_decl.c) as the declaration-ban call site.

#pragma cccc checked begin
int *g_unchecked = 0;
#pragma cccc checked end

int main(void) {
    (void)g_unchecked;
    return 42;
}
