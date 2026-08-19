// CCCC_FLAGS: --stack-canaries
// Ticket #1082: emit_static_chain_var_addr (src/codegen_addr.c) and the
// sibling/parent nested-call static-link walk (src/codegen_expr.c) both
// hardcoded a bare -8 for every static-link hop past the first, assuming
// every ancestor's own __static_link lives at bp[-1]. --stack-canaries (and
// -3, which bundles it) shifts every frame's params one slot lower --
// bp[-2] -- so a chain of depth >= 2 dereferenced the wrong slot and
// SIGSEGV'd (exit 139) walking through garbage. A depth-1 chain (only the
// first, canary-aware hop) was unaffected, which is why this went unnoticed:
// tools/tests.py never runs with --stack-canaries/-3 at all.
//
// Filed with a wrong root cause (CHKP3/dangling-pointer stack-liveness
// bookkeeping) -- it's neither: --stack-canaries alone reproduces with every
// other safety flag (--dangling-pointers, --bounds-checks, --uaf-detection,
// --stack-instrumentation, --checked-pointers) passing clean. Fixed by
// static_link_hop_bytes(vm), a single canary-aware hop distance shared by
// both call sites instead of two independent hardcodes.

// (a): depth-1 control -- already worked pre-fix, guards against a fix that
// only helps depth >= 2 while breaking the base case.
static int depth1(void) {
    int g = 7;
    int level1(int a) { return g + a; }
    return level1(3);
}

// (b): depth-2 -- the ticket's own repro shape.
static int depth2(void) {
    int g = 7;
    int level1(int a) {
        int level2(int b) { return g + a + b; }
        return level2(2);
    }
    return level1(1);
}

// (c): depth-3 -- a wrong-but-consistent hop distance could still pass (b)
// by accident; this is the case that actually catches an off-by-one in the
// per-hop loop.
static int depth3(void) {
    int g = 7;
    int l1(int a) {
        int l2(int b) {
            int l3(int c) { return g + a + b + c; }
            return l3(3);
        }
        return l2(2);
    }
    return l1(1);
}

// (d): a nested function calling a *sibling* nested function two levels up
// its own chain (codegen_expr.c's calling_nested walk, not gen_addr's own
// ND_VAR chase) -- the second call site carrying the identical hardcode.
// l4 -> l3 -> l2 is two loop iterations (callee_parent is l1, l4's own
// parent_fn is l3): confirmed this exercises >= 2 hops in that walk
// specifically, not just 1 -- a one-hop-only case can't distinguish a
// wrong-but-consistent hop distance from a correct one, the same reasoning
// that made depth3() (not depth2()) the real regression guard above.
static int sibling_chain(void) {
    int g = 7;
    int l1(int a) {
        int sib(int x) { return g + a + x; }
        int l2(int b) {
            int l3(int c) {
                int l4(int d) { return sib(d); }
                return l4(c);
            }
            return l3(b);
        }
        return l2(4);
    }
    return l1(1);
}

int main(void) {
    if (depth1() != 10) return 1;
    if (depth2() != 10) return 2;
    if (depth3() != 13) return 3;
    if (sibling_chain() != 12) return 4;
    return 42;
}
