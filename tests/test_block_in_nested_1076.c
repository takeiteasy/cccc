// #1076: a block literal defined inside a genuinely nested function (not a
// block), capturing a variable owned not by that nested function itself but
// by one of *its own* ancestors, silently miscompiled -- a wrong-answer bug
// on the plain VM path, no -c=native involvement at all.
//
// Root cause, two parts (both needed together):
//
// (1) block_literal()'s transitive-capture ancestor climb
//     (parse_blocks.c) only ever walked through *block* ancestors
//     (`anc->is_block`) looking for a `block_outer_locals` snapshot -- a
//     genuinely nested function never recorded one at all, so the climb
//     stopped dead the moment it reached one, and a variable owned by a
//     further-out ancestor never entered the block's own `captures` list.
// (2) Even once captured, the block's own descriptor-population loop
//     (codegen_expr.c's ND_BLOCK_LITERAL case) had no arm for "this capture
//     is owned by an ancestor of the *enclosing* nested function" -- only a
//     plain bp+offset read of the enclosing frame, or a read through an
//     enclosing *block's* own descriptor. Reading the wrong frame's bytes
//     is exactly what produced the ticket's own garbage exit code (241,
//     nondeterministic).
//
// Fixed by recording block_outer_locals for nested functions too
// (parse_decl.c) so the same ancestor climb blocks already use also works
// through a nested-function ancestor, and by adding the missing
// belongs_to_outer_function()-based static-link-chase arm to the capture
// loop (codegen_expr.c), sharing the exact chase gen_addr's own outer-
// function ND_VAR case already used (factored into
// emit_static_chain_var_addr, codegen_addr.c) so the two can't drift.
//
// NOT covered here: a nested function defined *inside* a block, referencing
// a variable owned by the block's own enclosing function -- confirmed a
// distinct, pre-existing bug (unaffected by this fix either way): a block's
// own __static_link slot holds a descriptor pointer, not a frame bp, which
// breaks the generic "-8 hop per ancestor level" chain-walk assumption the
// instant an intermediate ancestor is a block rather than a genuine nested
// function. Filed separately.

// (a) the ticket's own repro.
static int ticket_repro(void) {
    int g = 7;
    int mid(int m) {
        int (^blk)(void) = ^{
          return g + m;
        };
        return blk();
    }
    return mid(3);
}

// (b) control: block captures only its immediate nested-function parent's
// own local -- the already-working path this fix must not disturb.
static int own_local_only(void) {
    int mid(int m) {
        int (^blk)(void) = ^{
          return m;
        };
        return blk();
    }
    return mid(3);
}

// (c) control: block-in-block capturing a grandparent local -- the other
// already-working transitive-capture path.
static int block_in_block(void) {
    int g              = 7;
    int (^outer)(void) = ^{
      int (^inner)(void) = ^{
        return g;
      };
      return inner();
    };
    return outer();
}

// (d) two-level nesting: the block sits inside a nested function that is
// itself nested inside another, capturing the outermost's local (exercises
// the multi-hop static-link chain, not just a single hop).
static int two_level_nested(void) {
    int g = 7;
    int level1(int a) {
        int level2(int b) {
            int (^blk)(void) = ^{
              return g + a + b;
            };
            return blk();
        }
        return level2(2);
    }
    return level1(1); // expect 7 + 1 + 2 = 10
}

// (e) write-then-read through a __block-storage ancestor local, captured by
// a block nested two levels down -- exercises the is_block_var arm of the
// new static-link-chase capture source.
static int block_var_ancestor(void) {
    __block int g = 1;
    int mid(int m) {
        int (^blk)(void) = ^{
          g += m;
          return g;
        };
        return blk();
    }
    int r1 = mid(4); // g: 1 -> 5
    int r2 = g;      // write must be visible in mid's ancestor frame too
    return (r1 == 5 && r2 == 5) ? 1 : 0;
}

// (f) a struct capture from a grandparent (wider than 8 bytes), exercising
// the MCPY arm of the new static-link-chase capture source.
struct Pair {
    long a;
    long b;
};
static int struct_ancestor(void) {
    struct Pair p = {1, 41};
    int mid(void) {
        int (^blk)(void) = ^{
          return (int)(p.a + p.b);
        };
        return blk();
    }
    return mid();
}

int main(void) {
    if (ticket_repro() != 10)
        return 1;
    if (own_local_only() != 3)
        return 2;
    if (block_in_block() != 7)
        return 3;
    if (two_level_nested() != 10)
        return 4;
    if (block_var_ancestor() != 1)
        return 5;
    if (struct_ancestor() != 42)
        return 6;
    return 42;
}
