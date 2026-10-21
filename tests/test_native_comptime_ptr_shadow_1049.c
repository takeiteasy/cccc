// a GetComptimePtr() shadow Obj (make_comptime_shadow_obj, src/macros.c) used
// to be linked onto vm->compiler.globals -- a scratch per-TU list -- by
// link_comptime_shadow_objs(), running *after* main.c had already snapshotted
// merged_prog. The shadow never reached codegen_func.c's data-segment
// offset-allocation loop (which walks `prog`), so its offset stayed 0 and every
// GetComptimePtr() result silently aliased data_seg[0] -- a wrong answer on the
// plain VM path, not just a -c=native gap. Fixed by appending each shadow onto
// `prog`'s own tail at the end of cc_expand_macros(), which both codegen and
// the serializer's rename/definition passes already walk. Asserts VM 42 ->
// native 42, with a sentinel global and distinct per-shadow values so a
// regression to shared-address aliasing can't coincidentally sum to 42 again
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
