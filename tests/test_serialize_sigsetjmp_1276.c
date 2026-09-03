// sigsetjmp/siglongjmp/sigjmp_buf support (child of the self-hosting spike).
//
// Before this, cccc's own frontend had zero support for the sig* family:
// `sigjmp_buf env;` failed to parse ("expected ','"), because unlike
// setjmp/longjmp/_setjmp/_longjmp -- hand-registered compiler builtins with
// dedicated VM opcodes and -c=native lowering -- sigsetjmp/siglongjmp had no
// type, no builtin Obj, no codegen, and no serializer case. src/host_signal.c's
// own SIGSEGV/SIGBUS recovery guard is the first cccc source that asks cccc to
// parse this construct.
//
// On the VM the sig* pair aliases the plain SETJMP/LONGJMP opcodes (the VM has
// no signal mask) and the `savemask` argument is evaluated for side effects
// then discarded. Under -c=native they lower to the real host
// sigsetjmp()/siglongjmp() (on glibc: __sigsetjmp) so the mask save/restore is
// genuine.
//
// This test proves three things at once, on both the VM and the --native
// corpus:
//   1. `sigjmp_buf` parses as a struct member;
//   2. sigsetjmp returns 0 on the direct call and the siglongjmp value after;
//   3. the savemask argument is still evaluated (side effect observed);
//   4. an `unsigned long` canary placed immediately after the sigjmp_buf
//      survives -- the CCCC-sized buffer is large enough for the real host's
//      own, larger sigjmp_buf write under -c=native (same soundness class as
//      test_serialize_setjmp_1054.c).

#include <setjmp.h>

struct canary_layout {
    sigjmp_buf    env;
    unsigned long canary;
};

static struct canary_layout g;
static int                  savemask_evaluated;

static int savemask_arg(void) {
    savemask_evaluated = 1;
    return 0;
}

static void unwind(void) {
    siglongjmp(g.env, 42);
}

int main(void) {
    g.canary = 0xC0FFEE1276UL;

    int rv   = sigsetjmp(g.env, savemask_arg());
    if (rv == 0) {
        unwind();
        return 1; // unreachable
    }

    if (!savemask_evaluated)
        return 2; // savemask argument was not evaluated
    if (g.canary != 0xC0FFEE1276UL)
        return 3; // real host sigsetjmp() overran the buffer

    return rv;
}
