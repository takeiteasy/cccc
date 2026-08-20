// Ticket #1075: a nested (non-static) function *definition* whose name
// matches an enclosing file-scope function used to bind to -- and then
// "redefine" -- that outer function, instead of C block scope introducing
// a distinct declaration (C17 6.2.1p4: scope and linkage are separate
// axes; #1039 already established nested functions are implicitly static,
// invisible outside their own scope).
//
// Root cause: function() (src/parse_decl.c) looked the name up via
// find_func(), which walks the ENTIRE enclosing scope chain to file scope
// -- correct for a bodyless block-scope prototype (#1056's own guarantee:
// external linkage, must bind to the outer function), wrong for a nested
// *definition* (must never merge with an outer Obj).
//
// Fixed by widening the lookup to also use find_func_in_current_scope()
// (already used for `static`) whenever a nested function definition (not
// just a bodyless declaration) is being parsed, matched by whether a body
// actually follows -- checked the same way as the existing bodyless
// early-return (consume(";")), not equal(tok, "{"), since that would miss
// a K&R definition (#1043's lesson).
//
// A second, independent defect surfaced once two same-named Objs could
// legally coexist: -c=native hoists a nested function to file scope under
// its own name (#1074), which collided with the outer, non-static
// function of the same name -- rename_colliding_static_names()
// (src/serialize.c) previously only tracked `is_static` names and skipped
// any same-file collision on the (now-stale) assumption that one was
// always a parse-time redefinition error. Fixed by giving every non-static
// defining Obj's name an "anchor" entry that a same-named static/nested Obj
// must always yield to, checked in a pass ahead of prog's own list order
// (a nested function's Obj is always pushed ahead of its enclosing
// function's own, so a single combined pass would rename the wrong side).

// (a) The ticket's own repro shape, with a DIFFERENT body than the outer
// `add` so a wrong binding (calling the outer instead of the nested one,
// or vice versa) is visible in the result, not just in whether it compiles.
int add(int a, int b) {
    return a + b;
}

// (b) The outer function called from another, unrelated file-scope
// function -- must stay bound to the real outer `add`, unaffected by any
// later nested `add` definition inside main().
int uses_outer_add(void) {
    return add(10, 5);
} // 15

int test_shadowed_definition(void) {
    // Nested `add` shadows the outer one for the rest of this block --
    // deliberately a different arithmetic (a + b + 1) so a mis-binding to
    // the outer `add` is caught by the expected value below.
    int add(int a, int b) {
        return a + b + 1;
    }

    int r = add(40, 1); // nested: 40 + 1 + 1 == 42
    if (r != 42)
        return 1;

    return 0;
}

// (c) A recursive nested function shadowing a file-scope name of a
// DIFFERENT arity -- proves the nested Obj's own recursive call site
// resolves to itself, not to (or confused with) the outer `add`.
int test_recursive_shadow(void) {
    int add(int n) {
        if (n <= 1)
            return 1;
        return n + add(n - 1); // 5+4+3+2+1 == 15
    }
    if (add(5) != 15)
        return 2;
    return 0;
}

// (d) A `static` nested variant -- already worked before this fix (the
// `attr->is_static` arm already used find_func_in_current_scope()); kept
// as a regression guard now that the non-static path shares similar logic.
int test_static_nested_shadow(void) {
    static int add(int a, int b) {
        return a + b + 100;
    }
    if (add(1, 1) != 102)
        return 3;
    return 0;
}

// (e) #1056's own guarantee, unaffected by this fix: a bodyless
// block-scope prototype of an already-defined, DIFFERENT outer function
// must still bind to it (external linkage, C17 6.2.2p5), not be treated as
// introducing anything nested.
int test_prototype_still_binds_outer(void) {
    int uses_outer_add(void); // redundant block-scope prototype
    if (uses_outer_add() != 15)
        return 4;
    return 0;
}

int main(void) {
    if (uses_outer_add() != 15) // sanity check before any nested add exists
        return 10;

    int r;
    if ((r = test_shadowed_definition()) != 0)
        return 20 + r;
    if ((r = test_recursive_shadow()) != 0)
        return 20 + r;
    if ((r = test_static_nested_shadow()) != 0)
        return 20 + r;
    if ((r = test_prototype_still_binds_outer()) != 0)
        return 20 + r;

    if (add(40, 2) !=
        42) // outer add still intact at file scope after all of the above
        return 30;

    return 42;
}
