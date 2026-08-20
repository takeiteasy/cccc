// Ticket #1074: a genuine GNU nested function (Obj.is_nested, not an Apple
// block) is hoisted to file scope under -c=native with a synthesized
// `void *__static_link` first parameter (the parser already puts a real
// param Obj there, parse_decl.c) -- but nothing ever taught a call site to
// actually pass it, and a reference to an enclosing function's local from
// inside a nested body printed as a bare identifier that doesn't exist at
// file scope. `grep -n is_nested src/serialize.c` used to return nothing at
// all.
//
// Fixed by lowering nested functions the same way Apple blocks already are
// (#965): every function that directly parents a nested function gets a
// `struct __cccc_nenv_<name> { void *__up; T0 *__uv0; ... }` env struct at
// file scope (serialize_nested_preamble(), src/serialize.c), an instance
// (`__cccc_nenv`) declared and populated at the top of its own body, and
// every direct call to a nested function passes the right env pointer as
// its static link -- its own env for a direct child, or a chase through
// `->__up` (mirroring codegen_expr.c's calling_nested walk exactly) for a
// sibling or an ancestor's nested function. A reference to an outer
// local/param from inside a nested body is rewritten to
// `(*env->__uvK)` instead of the bare name.
//
// (a): no upvars at all -- just the hidden static-link plumbing.
static int outer_no_upvar(int base) {
    int inner_no_upvar(int x) {
        return x * 2;
    }
    return inner_no_upvar(base);
}

// (b): read AND write of an outer *local* (not a global -- a global needs
// no static-link help at all, so this is the genuine upvar-write case,
// same shape as test_suite_functions.c's update_outer).
static int outer_rw_local(void) {
    double v = 3.14;
    void set_v(double val) {
        v = val;
    }
    set_v(99.9);
    return (v == 99.9) ? 1 : 0;
}

// (c): two-level nesting -- inner reads a variable owned by its
// grandparent, not its immediate parent, exercising the ->__up chase.
static int outer_multilevel(void) {
    int g = 5;
    int mid(int m) {
        int inner_multilevel(int n) {
            return n + g;
        }
        return inner_multilevel(m) * 2;
    }
    return mid(10);
}

// (d): recursion -- a nested function calling itself must forward its own
// static link unchanged, not re-derive a new one.
static int outer_recursive(void) {
    int fact(int n) {
        if (n <= 1)
            return 1;
        return n * fact(n - 1);
    }
    return fact(5);
}

// (e): sibling nested functions sharing the same parent's locals, one
// calling the other two -- exercises "calling_nested but callee_parent ==
// current_fn" from inside a nested function itself, not just from the
// top-level parent.
static int outer_siblings(void) {
    int a = 1, b = 2;
    int get_a(void) {
        return a;
    }
    int get_b(void) {
        return b;
    }
    int sum_siblings(void) {
        return get_a() + get_b();
    }
    return sum_siblings();
}

int main(void) {
    if (outer_no_upvar(21) != 42)
        return 1;
    if (outer_rw_local() != 1)
        return 2;
    if (outer_multilevel() != 30)
        return 3;
    if (outer_recursive() != 120)
        return 4;
    if (outer_siblings() != 3)
        return 5;
    return 42;
}
