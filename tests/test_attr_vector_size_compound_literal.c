// GNU vector_size compound literals, `(v4sf){1,2,3,4}` (tracker #713
// follow-up to #72). Compound literals route through the same
// lvar_initializer/gvar_initializer machinery as ordinary declarations
// (postfix's compound-literal branch in parse.c), so they work as soon as
// TY_VECTOR brace-init does; this locks that in for a local temporary used
// directly in an expression, a `static` compound literal (address-of form),
// and a compound literal as the entire initializer of another global
// variable (fixed alongside the general struct/array/vector case in #720 --
// see test_gvar_init_from_compound_literal.c for the non-vector coverage).

typedef float v4sf __attribute__((vector_size(16)));

v4sf          global_lit = (v4sf){1.0f, 2.0f, 3.0f, 4.0f};

int main(void) {
    // Local (auto-storage) compound literal used directly in an expression.
    v4sf a = (v4sf){1.0f, 2.0f, 3.0f, 4.0f};
    if (a[0] != 1.0f)
        return 1;
    if (a[3] != 4.0f)
        return 2;

    v4sf sum = a + (v4sf){10.0f, 10.0f, 10.0f, 10.0f};
    if (sum[0] != 11.0f)
        return 3;
    if (sum[3] != 14.0f)
        return 4;

    // `static` compound literal (GNU/C23 storage-class-specified compound
    // literal -- an anonymous global under the hood, per
    // compound_literal_type's storage-class handling in parse.c).
    v4sf *bp = &(static v4sf){5.0f, 6.0f, 7.0f, 8.0f};
    if ((*bp)[0] != 5.0f)
        return 5;
    if ((*bp)[3] != 8.0f)
        return 6;

    // File-scope compound literal as the entire initializer.
    if (global_lit[0] != 1.0f)
        return 7;
    if (global_lit[3] != 4.0f)
        return 8;

    return 42;
}
