// a nested function defined INSIDE a block, reading a variable owned by the
// block's own enclosing function, is a distinct VM miscompile from's mirror
// nesting order -- broken on BOTH back-ends, not just -c=native. VM root cause:
// emit_static_chain_var_addr()'s static-link chase (codegen_addr.c) assumed
// every intermediate ancestor's own __static_link slot holds a plain frame bp;
// a block's own slot holds its descriptor pointer instead, so a chase that
// needs to hop THROUGH a block ancestor (depth >= 2 only) silently misread
// descriptor bytes as a frame pointer. Fixed by detecting the nearest block
// ancestor on the chase and terminating there, reading the variable out of that
// block's own capture descriptor -- which requires block_literal()'s
// transitive-capture climb (parse_blocks.c) to also walk every nested function
// defined directly inside a block's own body (Obj.nested_children,
// parse_decl.c), so a variable referenced only inside such a nested function
// still ends up captured. Design decision: the nested function sees the block's
// OWN creation-time snapshot of an ancestor-owned variable (by-value, like a
// sibling direct block read already does), not a live read --
// snapshot_consistency() pins this by mutating the ancestor variable between
// block creation and invocation. -c=native was independently broken too
// (compiled clean, segfaulted at runtime) -- its own nested-function-upvar
// machinery (NestedEnvEntry) was applied to a block ancestor as if it were a
// real nested function's env, chasing the block's real __static_link (its
// descriptor pointer) as another such env; fixed by stopping at the block and
// reading its real descriptor instead (block_ancestor_desc_ptr_expr(),
// serialize.c). Asserts VM 42 -> native 42.
int ticket_repro(void) {
    int g           = 7;
    int (^blk)(int) = ^(int m) {
      int inner(void) {
          return g + m;
      }
      return inner();
    };
    return blk(3);
}
int snapshot_consistency(void) {
    int g            = 5;
    int (^blk)(void) = ^{
      int inner(void) {
          return g;
      }
      return inner();
    };
    g = 100;
    return blk();
}
int block_var_write(void) {
    __block int g    = 1;
    int (^blk)(void) = ^{
      int inner(void) {
          g += 3;
          return g;
      }
      return inner();
    };
    int r1 = blk();
    int r2 = g;
    return (r1 == 4 && r2 == 4) ? 1 : 0;
}
int plain_write_stays_in_snapshot(void) {
    int g            = 5;
    int (^blk)(void) = ^{
      int inner(void) {
          g = 99;
          return g;
      }
      return inner();
    };
    int r = blk();
    return (r == 99 && g == 5) ? 1 : 0;
}
int main(void) {
    if (ticket_repro() != 10)
        return 1;
    if (snapshot_consistency() != 5)
        return 2;
    if (!block_var_write())
        return 3;
    if (!plain_write_stays_in_snapshot())
        return 4;
    return 42;
}
