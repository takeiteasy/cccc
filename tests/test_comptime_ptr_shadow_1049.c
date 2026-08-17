// Ticket #1049: GetComptimePtr()'s shadow Obj (make_comptime_shadow_obj,
// src/macros.c) used to be linked onto vm->compiler.globals -- a scratch
// per-TU list -- rather than the merged program (macro_globals/prog), and
// only *after* main.c had already snapshotted merged_prog. The shadow never
// reached codegen_func.c's data-segment offset-allocation loop, so its
// offset stayed 0 and every GetComptimePtr() result silently aliased
// data_seg[0] -- a wrong answer on the plain VM path, not just a -c=native
// gap (test_comptime_ptr_303.c happened to still sum to 42 with the bug
// present, so it never caught this; see its own updated comment).
//
// A file-scope runtime global ("sentinel") is declared first so a
// still-broken build reads its value instead of the intended comptime var,
// making the failure mode visible rather than a second coincidental pass.

int sentinel = 99;

[[cccc::comptime]]
int a = 7;

[[cccc::comptime]]
int b = 35;

[[cccc::comptime]]
Node *a_ptr(void) {
    return GetComptimePtr("a");
}

[[cccc::comptime]]
Node *b_ptr(void) {
    return GetComptimePtr("b");
}

int main(void) {
    int *x = a_ptr();
    int *y = b_ptr();
    if (x == y)
        return 1;
    if (*x != 7)
        return 2;
    if (*y != 35)
        return 3;
    if (sentinel != 99)
        return 4;
    return *x + *y;
}
