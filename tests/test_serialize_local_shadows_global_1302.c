// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: (?=[\s\S]*int local_shadows_global_1302_helper__cccc_\d+;)(?=[\s\S]*int local_shadows_global_1302_g__cccc_\d+;)
// CCCC_REJECT_STDOUT: \n {4}int local_shadows_global_1302_helper;\n
//
// #1302: `-c=native`/`-m` flattens every local of a function to one
// top-of-function C89-style declaration list (serialize_function(),
// src/serialize_decl.c) -- that hoist widens a block-scoped local's scope
// to the whole function. Found via #1132's round-13 self-hosting spike
// (src/parse_postfix.c's primary(): a local `Node *expr` inside the
// constexpr_expr_for_node()-using branch, hoisted, then shadowed the
// later, unrelated call to the global expr() parser function in the same
// function).
//
// Two independent shapes below, both reduced from that repro and both
// confirmed as pre-fix regressions with a standalone minimal .c (no cccc
// source involved):
//
//   - local_shadows_global_1302_use_fn: a block-scoped local named exactly
//     like a global *function* (local_shadows_global_1302_helper), with an
//     unrelated call to that global appearing later in the same function,
//     outside the local's own block. Pre-fix, hoisting `int
//     local_shadows_global_1302_helper;` to the top of the function turned
//     the later `local_shadows_global_1302_helper(a)` call into "called
//     object type 'int' is not a function or function pointer" -- a host
//     *compile* failure.
//
//   - local_shadows_global_1302_use_var: the more dangerous variant --
//     a block-scoped local named like a global *variable*
//     (local_shadows_global_1302_g). This one does NOT fail to compile: the
//     hoisted local silently shadows the global for the rest of the
//     function, so `a + local_shadows_global_1302_g` after the `if` reads
//     the local's last value instead of the global. See
//     tools/comptime_native_smoke.py's case_local_shadows_global_1302 for
//     the full VM-vs-native divergence this produces end to end (a wrong
//     answer, not a compile error, so it needs the smoke test's exit-code
//     comparison, not this file's -m shape check, to actually catch it).
//
// Fixed by widening the existing #926 hoisted-local collision-rename loop
// (src/serialize_decl.c) to also check a hoisted local's name against
// every global this function's body actually references (reference-gated,
// via a lazily built ctx->global_names index + a serialize_ast_walk()
// collection of this function's own ND_VAR references -- so a program
// with no collision emits byte-identical output), renaming the *local*
// (never the global, which may have external callers/prototypes already
// emitted) with the same "%s__cccc_%d" scheme #926 already uses.
int local_shadows_global_1302_helper(int x) {
    return x + 1;
}

int local_shadows_global_1302_g = 7;

int local_shadows_global_1302_use_fn(int flag, int a) {
    if (flag) {
        int local_shadows_global_1302_helper = a * 2;
        return local_shadows_global_1302_helper;
    }
    return local_shadows_global_1302_helper(a);
}

int local_shadows_global_1302_use_var(int flag, int a) {
    if (flag) {
        int local_shadows_global_1302_g = a * 2;
        a                               = local_shadows_global_1302_g;
    }
    return a + local_shadows_global_1302_g;
}

int main(void) {
    if (local_shadows_global_1302_use_fn(0, 41) != 42)
        return 1;
    if (local_shadows_global_1302_use_var(0, 42) != 49)
        return 1;
    return 42;
}
