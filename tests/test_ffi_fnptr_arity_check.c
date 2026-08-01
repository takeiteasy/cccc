// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --ffi-type-checking
//
// Ticket V010 (#874 follow-on): before the fix, op_CALLN_fn's FFI-token
// branch derived its argument count from ff->num_args (clamped to 8)
// instead of the callsite's own encoded nargs, so --ffi-type-checking's
// arity check could never see a real mismatch on an indirect call --
// this cast-through-a-wrong-arity-pointer call would have silently
// under-called strcmp with 2 args instead of erroring on the 3 actually
// passed. Now the real callsite nargs reaches cccc_check_ffi_policy.
int strcmp(const char *s1, const char *s2);

int main(void) {
    int (*p)(const char *, const char *, const char *) =
        (int (*)(const char *, const char *, const char *))strcmp;
    return p("a", "b", "c");
}
