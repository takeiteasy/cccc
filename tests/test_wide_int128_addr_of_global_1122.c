// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not a compile-time constant
//
// #1122: eval_wide's narrow-operand fallback (and the pre-existing narrow
// bitfield path, `eval(vm, expr)`) call eval2() with label==NULL -- there is
// no relocation channel for a >8-byte scalar or a bitfield's read-modify-
// write. An address-of-global expression reaching eval_rval()'s ND_VAR arm
// with label==NULL used to dereference that NULL unconditionally
// (`*label = &node->var->name;`), segfaulting instead of erroring, for both
// a wide destination (the __int128 case below, newly reachable through
// eval_wide) and a narrow one (`struct S { int f : 20; }; static struct S s
// = { (int)&x };`, pre-existing and unrelated to #1122's own code, but
// fixed at the same time since it's the identical hazard one level up the
// call chain). Guarding only ND_VAR (not ND_MEMBER) matters here too: an
// earlier, broader version of this guard also rejected offsetof(T, m) in a
// constant expression, since offsetof's `&(((T*)0)->m)` expansion bottoms
// an ND_MEMBER/ND_DEREF chain out at a null-pointer constant and folds fine
// with label==NULL -- see eval_rval's own comment in src/parse_analysis.c.
static int      x;
static __int128 g = (__int128)&x;

int main(void) {
    return 42;
}
