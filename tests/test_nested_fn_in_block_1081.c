// #1081: a nested function defined *inside a block*, reading a variable
// owned by the block's own enclosing function, is a distinct VM miscompile
// from #1076/#1080 (the mirror nesting order) -- broken on BOTH back-ends,
// not just -c=native.
//
// Root cause: belongs_to_outer_function() already resolves ownership
// correctly (a normal nested-function static-link lookup, not a block
// capture-list walk -- #1076's own parse-time gap doesn't apply here). The
// bug is in the static-link CHASE itself (emit_static_chain_var_addr,
// codegen_addr.c): every intermediate ancestor's own __static_link slot is
// assumed to hold a plain frame bp, and the chase hops through it uniformly.
// A block's own __static_link slot instead holds its DESCRIPTOR pointer
// (populated at block-creation time, ND_BLOCK_LITERAL in codegen_expr.c) --
// the chase's hop through it silently misreads descriptor bytes as a frame
// pointer the instant it needs to hop *through* a block (depth >= 2 only; a
// direct child reading the block's own param/local, depth == 1, already
// worked -- see control_own_param_and_local below).
//
// Fixed with a block-aware chase: emit_static_chain_var_addr now detects the
// nearest block ancestor strictly between the current function and the
// variable's real owner and terminates there, reading the variable out of
// that block's own capture descriptor instead of continuing to hop through
// it as a frame pointer. This requires the variable to actually be captured
// by that block -- parse_blocks.c's block_literal() now also walks every
// nested function defined directly inside a block's own body (Obj.
// nested_children, recorded by parse_decl.c) when collecting transitive
// captures, so a variable referenced only inside such a nested function
// still ends up in the block's own captures list.
//
// Design decision (user sign-off): a nested function inside a block sees
// the block's OWN creation-time snapshot of an ancestor-owned variable --
// exactly like a sibling direct read (`^{ return g; }`) in the same block
// already does -- not a live read of the ancestor's frame. There is no
// reference implementation to defer to for this exact combination (clang
// has blocks but no nested functions; gcc the reverse), so internal
// consistency with the block's own direct captures is the spec; write-
// propagation requires __block, the same rule blocks already have. See
// snapshot_consistency() below, which pins this by mutating the ancestor
// variable between block creation and invocation.
//
// The mirror shape (a nested function whose own parent sits *beyond* a
// block ancestor of the current function, reached only by climbing out of
// a block first -- a call, not a variable read) is a distinct, pre-existing
// bug this fix does NOT cover: it needs the block's *enclosing frame*, which
// a heap-copyable block's descriptor deliberately never stores. Both back
// ends now reject that shape with a diagnostic ("#1081 residual") rather
// than silently miscompiling; filed as its own follow-up.
//
// -c=native was ALSO independently broken for this whole shape (not merely
// rejected like #1080's mirror case) -- it compiled clean and segfaulted at
// runtime, since serialize.c's own nested-function-upvar machinery
// (NestedEnvEntry/__cccc_nenv_*) was, before this fix, applied uniformly to
// a block ancestor as if it were a real nested function's env, chasing a
// block's own real __static_link (its descriptor pointer) as if it were
// another such env. Fixed by making record_nested_upvar()/serialize_nested_
// upvar_ref() stop at the nearest block ancestor exactly like the VM's own
// chase, and reading the variable out of the block's real descriptor
// (block_ancestor_desc_ptr_expr(), serialize.c) instead.

// (a) the ticket's own repro.
static int ticket_repro(void) {
    int g           = 7;
    int (^blk)(int) = ^(int m) {
      int inner(void) {
          return g + m;
      }
      return inner();
    };
    return blk(3) == 10 ? 1 : 0;
}

// (b) pins the by-value snapshot decision: mutating the ancestor variable
// AFTER the block is created but BEFORE it's invoked must not be visible to
// a nested function reading it through the block's descriptor -- it must
// see the same creation-time snapshot a sibling direct read in the same
// block already sees.
static int snapshot_consistency(void) {
    int g            = 5;
    int (^blk)(void) = ^{
      int inner(void) {
          return g;
      }
      return inner();
    };
    g     = 100;
    int r = blk();
    return r == 5 ? 1 : 0;
}

// (c) __block-storage ancestor: a nested function's write through the
// shared heap box must be visible back in the ancestor's own frame --
// unlike (b), a __block local is explicitly the escape hatch for live
// read/write, not a snapshot.
static int block_var_write(void) {
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

// (d) block-in-block transitivity: the variable is referenced ONLY inside
// the innermost nested function, two block levels down -- every
// intervening block on the chain must have transitively captured it.
static int block_in_block_transitive(void) {
    int g                  = 9;
    int (^outer_blk)(void) = ^{
      int (^inner_blk)(void) = ^{
        int deepest(void) {
            return g;
        }
        return deepest();
      };
      return inner_blk();
    };
    return outer_blk() == 9 ? 1 : 0;
}

// (e) a two-level nested-function chain inside a block, reading both the
// block's own param and its enclosing function's local.
static int two_level_nested_in_block(void) {
    int g           = 11;
    int (^blk)(int) = ^(int m) {
      int inner(void) {
          int inner2(void) {
              return g + m;
          }
          return inner2();
      }
      return inner();
    };
    return blk(2) == 13 ? 1 : 0;
}

// (f) a struct capture from a grandparent (wider than 8 bytes), read by a
// nested function inside a block -- exercises the same wide-capture
// descriptor slot as test_block_in_nested_1076.c's struct_ancestor(), from
// the mirror nesting order.
struct Pair {
    long a;
    long b;
};
static int struct_ancestor_nested(void) {
    struct Pair p    = {3, 39};
    int (^blk)(void) = ^{
      int inner(void) {
          return (int)(p.a + p.b);
      }
      return inner();
    };
    return blk() == 42 ? 1 : 0;
}

// (g) control: a nested function reading the block's OWN param and OWN
// local directly (depth == 1, no chase through a descriptor needed) --
// the already-working case this fix must not disturb.
static int control_own_param_and_local(void) {
    int (^blk)(int) = ^(int m) {
      int loc = 100;
      int inner(void) {
          return m + loc;
      }
      return inner();
    };
    return blk(5) == 105 ? 1 : 0;
}

// (h) a plain (non-__block) WRITE to an ancestor variable from a nested
// function inside a block: under by-value snapshot semantics the write
// must land in the block's own descriptor copy only -- invisible to the
// ancestor's own frame, unlike (c)'s __block escape hatch.
static int plain_write_stays_in_snapshot(void) {
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
    if (!ticket_repro())
        return 1;
    if (!snapshot_consistency())
        return 2;
    if (!block_var_write())
        return 3;
    if (!block_in_block_transitive())
        return 4;
    if (!two_level_nested_in_block())
        return 5;
    if (!struct_ancestor_nested())
        return 6;
    if (!control_own_param_and_local())
        return 7;
    if (!plain_write_stays_in_snapshot())
        return 8;
    return 42;
}
