// -c=native rejected a block literal inside a genuinely nested function
// capturing a variable owned by one of that function's own ancestors ('a block
// literal inside a nested function capturing ... is not supported (
// follow-up)') -- the VM-side miscompile itself was fixed by, but the
// native-lowering follow-up was never done: collect_nested_refs()'s
// ND_BLOCK_LITERAL arm now registers the ancestor-owned capture as an upvar of
// the real owner (record_nested_upvar()) instead of rejecting it, and the
// capture-copy loop in serialize_expr()'s ND_BLOCK_LITERAL case reads it back
// through the same env chase (nested_env_ptr_expr) an ordinary nested-function
// upvar reference uses. A __block-storage ancestor capture (previously rejected
// outright) is supported too, via an extra level of indirection in the env
// field (T ** instead of T *). Asserts VM 42 -> native 42 (both cases
// previously a hard compile-time rejection under -c=native).
int ticket_repro(void) {
    int g = 7;
    int mid(int m) {
        int (^blk)(void) = ^{
          return g + m;
        };
        return blk();
    }
    return mid(3);
}
int block_var_ancestor(void) {
    __block int g = 0;
    int mid(int m) {
        void (^blk)(void) = ^{
          g += m;
        };
        blk();
        return 0;
    }
    mid(4);
    return g;
}
int main(void) {
    if (ticket_repro() != 10)
        return 1;
    if (block_var_ancestor() != 4)
        return 2;
    return 42;
}
