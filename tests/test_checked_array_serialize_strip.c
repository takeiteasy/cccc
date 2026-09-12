// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int a\[10\]
// CCCC_REJECT_STDOUT: _Checked
//
// #487: `_Checked[N]`/`_Nt_checked[N]` must leave no trace in -m/-c=native/
// -c=generated output, same ABI-transparency contract the six checked-
// pointer attributes have -- a checked array serializes as a plain array
// declaration, since serialize_type_decl()'s TY_ARRAY branch never reads
// checked_kind/checked_array_extent at all.

int main(void) {
    int a _Checked[10];
    a[0] = 1;
    return a[0] == 1 ? 42 : 1;
}
