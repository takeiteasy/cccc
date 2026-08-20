// Ticket #1056: a bodyless block-scope function declaration used to
// retroactively flip an already-defined, already-codegen'd file-scope
// function to is_nested/is_static, corrupting the ABI of every call site
// emitted afterward -- a real host static-link argument the callee's body
// was never compiled to expect gets loaded into REG_A0, shifting every
// ordinary argument by one slot.
//
// Root cause: src/parse_decl.c's "Set up nested function tracking" block
// (shared by function() and declare_function_prototype()) ran on whatever
// Obj find_func() returned *before* checking whether a body actually
// follows -- so `int helper(int); return helper(41);` inside another
// function's body, which is a plain redundant block-scope prototype of the
// external `helper` (C17 6.2.2p5: no storage-class specifier => external
// linkage, not a nested function), got marked is_nested/is_static anyway.
//
// Fixed by moving nested-function tracking to only run once a body is
// confirmed present (function()'s post-";"-early-return path); the
// declare_function_prototype() copy (only ever reached bodyless, via
// function_declaration_list()) never needs it at all and had it removed.

// (a) The ticket's own failing case: a defined, parameterized file-scope
// function, block-scope-redeclared (redundant prototype) from inside
// another function's body, then called.
int helper(int x) {
    return x + 1;
}

// (b) No-arg control: nothing to shift, worked before the fix too --
// kept as a regression guard.
int helper2(void) {
    return 42;
}

// (c) File-scope-only redeclare-then-define, no block scope involved --
// worked before the fix too, kept as a regression guard.
int helper3(int x);
int helper3(int x) {
    return x + 1;
}

// (d) A genuine nested-function *definition* -- must still resolve its
// static link correctly (regression guard for #1039/the static-link path
// codegen_expr.c's calling_nested arm implements).
static int outer_with_nested(int base) {
    int inner(int x) {
        return x * 2;
    }
    return inner(base);
}

// (e) The multi-declarator variant of (a) -- exercises
// declare_function_prototype()/function_declaration_list(), not function().
int fa(int a) {
    return a + 1;
}
int fb(int a) {
    return a + 10;
}

int main(void) {
    {
        int helper(int); // redundant block-scope prototype
        int r = helper(41);
        if (r != 42)
            return 1;
    }

    if (helper2() != 42)
        return 2;

    if (helper3(41) != 42)
        return 3;

    if (outer_with_nested(21) != 42)
        return 4;

    {
        int fa(int), fb(int);        // multi-declarator block-scope prototype
        int r = fa(10) + fb(20) + 1; // 11 + 30 + 1 == 42
        if (r != 42)
            return 5;
    }

    return 42;
}
