// buffalo tracker #19: every generated wrapper around a struct-returning
// call carried a dead, unused local under -c=generated/-c=native.
//
// funcall() (src/parse_postfix.c) allocates a caller-side struct-return
// slot -- an empty-named Obj on Node.ret_buffer -- for every call whose
// return type is TY_STRUCT/TY_UNION. That slot is a VM-codegen-only
// concept: the RETBUF/VSTR convention (codegen_expr.c/codegen_emit.c)
// reads it purely as a frame offset. No serialize_*.c file ever references
// the Obj itself -- the serializer lowers a struct-returning call straight
// to plain C (serialize_expr.c's ND_FUNCALL case) -- yet the Obj still sat
// on fn->locals, so serialize_decl.c's hoist loop named it (the
// `__cccc_tmp%d` pass) and declared it, with nothing in the generated body
// ever reading or writing it:
//
//   struct S wrap(void) {
//       struct S __cccc_tmp0;   /* never read or written */
//       return mk(40, 2);
//   }
//
// -Wunused-variable under any warning build. This happened at EVERY
// struct-returning call site, not only a tail `return f();` -- the ticket's
// own framing was narrower than the actual bug.
//
// Fixed by tagging the ret_buffer Obj (Obj.is_ret_buffer,
// parse_postfix.c) and skipping it outright in serialize_decl.c's hoist
// loop (and its file-scope-anonymous-type-hoisting counterpart in
// serialize_program.c) -- no liveness walk needed, since no ND_VAR node
// ever names one of these regardless of which position the call appears
// in.
//
// This exercises all three positions a struct-returning call can appear in
// -- a tail return, an initializer, and a sub-expression -- and checks the
// round-tripped values, not just that the program compiles.

struct Pair {
    int a;
    int b;
};

struct Pair mk_19(int a, int b) {
    struct Pair p;
    p.a = a;
    p.b = b;
    return p;
}

// Tail return of a struct-returning call.
struct Pair wrap_tail_19(void) {
    return mk_19(1, 2);
}

// Struct-returning call as an initializer.
int use_init_19(void) {
    struct Pair p = mk_19(3, 4);
    return p.a + p.b;
}

// Struct-returning call as a sub-expression (assigned into an existing
// local, not a tail return or a fresh initializer).
int use_subexpr_19(void) {
    struct Pair p;
    p = mk_19(5, 6);
    return p.a + p.b;
}

int main(void) {
    struct Pair w = wrap_tail_19();
    if (w.a != 1 || w.b != 2)
        return 1;
    if (use_init_19() != 7)
        return 2;
    if (use_subexpr_19() != 11)
        return 3;

    return 42;
}
