// #1100 (WONT_FIX): calling a nested function whose own parent sits *beyond*
// a block ancestor -- a sibling/cousin call reached only by climbing OUT of
// a block first -- is a permanent, by-design refusal on BOTH back ends, not
// a native-only gap.
//
// Distinct from #1081 (a nested function inside a block reading an ancestor-
// owned *variable*, which #1081 fixed by reading the block's own creation-
// time capture snapshot). There is no snapshot-based answer here: `mid`
// below is a *function*, a sibling of `blk` owned by `test`, and reaching it
// from inside `blk` needs the block's own ENCLOSING FRAME -- which a heap-
// copyable block's descriptor deliberately never stores (a block must never
// retain a live pointer back to its creating frame, the same invariant
// #1081's snapshot decision was made on). A real fix would reverse that
// signed-off invariant and need new escape-analysis machinery, for a shape
// with no known real-world occurrence -- closed WONT_FIX.
//
// Both back ends reject with a diagnostic rather than miscompile:
//   VM      (codegen_expr.c, calling_nested static-link walk):
//     "calling a nested function whose parent is beyond a block ancestor
//      is not supported (#1081 residual)"
//   -c=native (serialize_program.c, collect_nested_refs):
//     "cannot serialize to native code: calling nested function 'mid',
//      whose own parent is beyond a block ancestor, is not supported
//      (#1081 residual)"
// This test pins that refusal on both paths -- #1100 itself closed docs-only
// with no regression coverage. The mirror shape (a variable read, not a
// call) is the supported #1081 case, covered by
// tests/test_nested_fn_in_block_1081.c.
//
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: beyond a block ancestor[\s\S]*#1081 residual

int test(void) {
    int g = 7;
    int mid(int m2) {
        return g + m2;
    }
    int (^blk)(void) = ^{
      int inner(void) {
          return mid(5);
      }
      return inner();
    };
    return blk();
}

int main(void) {
    return test() == 12 ? 42 : 1;
}
