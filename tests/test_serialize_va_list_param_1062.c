// Ticket #1062: a va_list forwarded as an ordinary function *parameter*
// used to silently disagree between the VM and -c=native on glibc
// specifically (macOS was already correct by coincidence).
//
// CCCC's own va_list (include/stdarg.h) is a plain struct, and every
// struct/union by-value parameter is genuinely copied in the callee's own
// prologue (#1078) -- so on the VM, `void helper(int n, va_list ap)` always
// gets its own independent copy; the caller's own va_list is never
// advanced by whatever the callee does with it.
//
// Native on macOS: the real va_list is a bare `char *`, an ordinary
// scalar -- also genuinely by-value, matching the VM by coincidence.
//
// Native on glibc (x86_64 and aarch64): the real va_list is
// `typedef struct __va_list_tag va_list[1]` -- an *array* type. A function
// parameter of array type always decays to a pointer (C17 6.7.6.3p7), so
// the callee's parameter secretly aliases the caller's own va_list; the
// callee's va_arg calls silently advance the *caller's* va_list too.
//
// Fixed with a callee-side va_copy shim (serialize_function_signature /
// serialize_function, src/serialize.c): the emitted parameter is renamed
// to __cccc_va_param_<name>, and the function body's own top gets an
// injected `va_list <name>; va_copy(<name>, __cccc_va_param_<name>);` --
// restoring the VM's own by-value semantics on every host, including
// glibc. See src/serialize.c's own #1062 comment (type_is_cccc_va_list /
// va_list_shim_param_name) for the full reasoning, including why this
// approach (rather than an array-of-1-struct VM ABI change, or diagnosing/
// rejecting the parameter under -c=native on Linux) was chosen.
//
// Confirmed failing pre-fix in the cccc-linux-amd64 container specifically
// (glibc x86_64) -- invisible on macOS by construction, since macOS's own
// va_list is already a scalar. Every case below asserts the *caller's* own
// va_list is unaffected by whatever the callee consumed from its copy.

#include "stdarg.h"

// Consumes exactly one argument from the forwarded va_list.
static int consume_one(int n, va_list ap) {
    (void)n;
    return va_arg(ap, int);
}

// Consumes zero arguments -- a callee that merely receives the parameter
// and never reads it should still leave the caller's own va_list intact
// (a control against a bug that miscounts even when nothing is read).
static void consume_none(int n, va_list ap) {
    (void)n;
    (void)ap;
}

// Two-level forward: caller -> mid (forwards its own received va_list a
// second time, to a third function) -> leaf (consumes one argument). Every
// hop needs its own independent copy for the outermost caller's va_list to
// stay unaffected, not just the first hop.
static int leaf_consume_one(va_list ap) {
    return va_arg(ap, int);
}
static int mid_forward(va_list ap) {
    return leaf_consume_one(ap);
}

// va_copy-then-forward: the caller makes its own explicit copy first, then
// forwards *that* copy as a parameter -- the copy passed to the callee
// should get its own further copy in turn, leaving the caller's explicit
// copy (not just its original va_list) unaffected too.
static int consume_from_copy(va_list ap) {
    return va_arg(ap, int);
}

static int test_basic_forward(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first    = va_arg(ap, int);    // consumes 10
    int consumed = consume_one(1, ap); // callee's own copy consumes 20
    int next     = va_arg(ap, int);    // caller's ap must still see 20
    va_end(ap);
    if (first != 10)
        return 1;
    if (consumed != 20)
        return 2;
    if (next != 20)
        return 3; // would be 30 if aliased
    return 0;
}

static int test_zero_consuming_forward(int count, ...) {
    va_list ap;
    va_start(ap, count);
    consume_none(1, ap);
    int first = va_arg(ap, int);
    va_end(ap);
    return first == 10 ? 0 : 1;
}

static int test_two_level_forward(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first     = va_arg(ap, int); // consumes 10
    int forwarded = mid_forward(ap); // mid->leaf's own copy consumes 20
    int next      = va_arg(ap, int); // caller's ap must still see 20
    va_end(ap);
    if (first != 10)
        return 1;
    if (forwarded != 20)
        return 2;
    if (next != 20)
        return 3;
    return 0;
}

static int test_copy_then_forward(int count, ...) {
    va_list ap, copy;
    va_start(ap, count);
    va_copy(copy, ap);
    int consumed  = consume_from_copy(copy); // consumes 10 from copy's own copy
    int from_copy = va_arg(copy, int);       // copy must still see 10
    int from_ap   = va_arg(ap, int); // ap (never forwarded) must see 10 too
    va_end(copy);
    va_end(ap);
    if (consumed != 10)
        return 1;
    if (from_copy != 10)
        return 2;
    if (from_ap != 10)
        return 3;
    return 0;
}

int main(void) {
    int r;
    if ((r = test_basic_forward(2, 10, 20)) != 0)
        return r;
    if ((r = test_zero_consuming_forward(1, 10)) != 0)
        return 10 + r;
    if ((r = test_two_level_forward(2, 10, 20)) != 0)
        return 20 + r;
    if ((r = test_copy_then_forward(1, 10)) != 0)
        return 30 + r;
    return 42;
}
