// CCCC_FLAGS: -3
// Ticket #1082 follow-up: same defect as test_static_chain_canary_1082.c,
// re-run under the full -3 safety tier (which bundles --stack-canaries with
// dangling-pointer detection and friends) rather than --stack-canaries
// alone, so the composed tier stays exercised too -- not just the single
// flag that isolates the bug. A depth-3 static-link chain is enough: see
// the other file for the fuller case breakdown and root-cause writeup.
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

int main(void) {
    return depth3() == 13 ? 42 : 1;
}
