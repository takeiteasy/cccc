// a const-qualified *pointer* (`int *const p`, is_const on the TY_PTR Type
// itself) used to serialize with its leading `const` misplaced onto the pointee
// -- serialize_type() (src/serialize.c) printed `const ` unconditionally, then
// fell through to the TY_PTR case, which recurses into serialize_type_decl() --
// whose TY_PTR branch never emits pointer-level const at all. Result: `const
// int (*)(void)` (pointer to const int) instead of `int (*const)(void)` (const
// pointer to int), rejected by the host compiler as an incompatible function
// pointer type; same bug latent in a function parameter's prototype vs.
// definition. Fixed by normalizing: a bare (non-typedef'd) pointer no longer
// prints pointer-level const here, matching what declarator position already
// did. Asserts VM 42 -> native 42
int rhandler_a(void) {
    return 100;
}
int rhandler_b(void) {
    return 200;
}
static int (*const rtable[])(void) = {rhandler_a, rhandler_b};
static int read_const(int *const p);
static int read_const(int *const p) {
    return *p;
}
int main(void) {
    if (rtable[0]() != 100)
        return 1;
    if (rtable[1]() != 200)
        return 2;
    int        x    = 7;
    int *const cp   = &x;
    void      *vp   = (void *)cp;
    int       *back = (int *)vp;
    if (*back != 7)
        return 3;
    int y = 9;
    if (read_const(&y) != 9)
        return 4;
    return 42;
}
