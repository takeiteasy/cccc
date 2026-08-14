// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not a struct nor a union
//
// #999: struct_ref()'s error-recovery paths (src/parse.c) return a bare
// ND_MEMBER node with lhs left NULL and ty = ty_error when the base
// expression isn't a struct/union -- a placeholder standing in for "this
// already failed to typecheck", not a real member access. to_assign()'s
// `A.x op= C` branch used to dereference that placeholder's NULL lhs
// unconditionally (`binary->lhs->lhs->ty`), crashing with SIGSEGV and no
// diagnostic at all instead of surfacing the "not a struct nor a union"
// error error_tok_recover() had already queued. Verified this exact
// program crashed (exit 139, no stderr) before the fix.
int main(void) {
    int notstruct = 0;
    notstruct.y += 1;
    return 42;
}
