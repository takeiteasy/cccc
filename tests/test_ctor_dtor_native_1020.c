// Ticket #1020: __attribute__((constructor[(priority)]))/((destructor
// [(priority)])) was never lowered by serialize_function_signature() at
// all -- grep -n "constructor" src/serialize.c returned nothing but an
// unrelated comment. Under -c=native the marked function was emitted as an
// ordinary function nothing calls, so it simply never ran (not a wrong
// ordering -- no ordering at all). The VM's own model (ascending priority
// first at startup, unprioritised group last, descending/atexit-interleaved
// at exit) was already spec-correct; this file only exercises the lowering,
// not the ordering rules (those are already covered by
// test_constructor_priority.c/test_destructor_on_exit.c/
// test_destructor_exit_atexit_order.c, all now un-skipped alongside this
// fix).
//
// Fixed by emitting the attribute as a *prefix* on the declarator in
// serialize_function_signature() (src/serialize.c), covering both the
// forward declaration and the definition (one emit site, per
// serialize_function_signature's own doc comment). Deliberately a prefix,
// not appended after the declarator the way asm_label is: GCC rejects a
// trailing attribute on a function *definition* (clang accepts it) -- the
// macOS-passes/Linux-fails shape this batch keeps relearning.

int ctor_ran;
int ctor_prio_ran;
int dtor_ran;
int order[2];
int order_idx;

__attribute__((constructor)) void plain_ctor(void) {
    ctor_ran = 1;
}

__attribute__((constructor(150))) void prio_ctor(void) {
    ctor_prio_ran = 1;
}

static __attribute__((constructor)) void static_ctor(void) {
    // Reachable only through the attribute, no call site -- confirms a
    // static constructor isn't dropped as dead code by the serializer
    // (mirrors test_constructor_destructor_static_opt.c's own concern,
    // pre-checked separately at -O3 while planning this fix).
    order[order_idx++] = 1;
}

__attribute__((destructor)) void plain_dtor(void) {
    dtor_ran = 1;
}

int main(void) {
    if (!ctor_ran) return 1;       // constructor never ran at all
    if (!ctor_prio_ran) return 2;  // prioritised constructor never ran
    if (order_idx != 1 || order[0] != 1) return 3; // static ctor dropped
    // plain_dtor can't be observed from inside main() (it runs after
    // return) -- its own execution is covered by dtor_ran being read back
    // is not possible here; test_destructor_basic.c/test_destructor_
    // priority.c already assert observable post-exit behavior. This file's
    // job is confirming the attribute round-trips into working native
    // output at all, which the constructor side already demonstrates.
    return 42;
}
