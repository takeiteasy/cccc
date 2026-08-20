// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int use_block\(void\);\s*\n\s*\nstatic int
// __cccc_block_0\(void \*__static_link\); CCCC_EXPECT_STDOUT: static int
// __cccc_block_0\(void \*__static_link\) \{ CCCC_C4_SKIP
//
// Ticket #995: a block literal lifted while building a macro-generated
// function body (FunctionSetBody(fn, Quote(...)) without WithFn(fn)) never
// got is_macro_generated set on its own synthesized function -- that flag
// gates cc_record_emit_object (macros.c) and the emit-events replay loop
// that emits function bodies (serialize.c), so under -c=generated the
// lifted block function was silently dropped from the output while
// use_block's body still called it, leaving generated C that fails to
// link. -m runs the same (!generated_only) serializer path as -c=native,
// which was already correct -- this file asserts the -c=generated shape
// directly: __cccc_block_0 is forward-declared ahead of use_block's body
// (the #956 on-demand forward-decl scan needed a new ND_BLOCK_LITERAL arm,
// since a block descriptor references its function through node->block_fn,
// not an ND_VAR child) and its definition is present at all.
//
// Not runnable end-to-end under -m (no -c=native link step here; the
// end-to-end VM 42 -> native 42 round trip lives in
// tools/comptime_native_smoke.py case 65), so this is a pure shape
// assertion.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("use_block", GetType("int"));
    FunctionSetBody(
        fn,
        Quote("{ int n = 42; int (^b)(void) = ^{ return n; }; return b(); }"));
    PublishNode(fn);
}
gen();

int use_block(void);

int main(void) {
    return use_block();
}
