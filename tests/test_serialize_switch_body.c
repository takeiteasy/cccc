// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: case 1:[\s\S]*?\+ 1;[\s\S]*?case 2:[\s\S]*?\+
// 100;[\s\S]*?break;[\s\S]*?case 3 \.\.\. 5:[\s\S]*?\+
// 1000;[\s\S]*?break;[\s\S]*?default:[\s\S]*?\+ 10000; CCCC_REJECT_STDOUT:
// unsupported expr kind
//
// #1005: the ND_SWITCH serializer arm used to reconstruct a switch from the
// case_next chain (newest-first, since parse.c prepends onto it) instead of
// serializing node->then -- so it emitted only each case's *first*
// statement (dropping every statement after it, including a case's own
// `break`), in reverse source order, with `default:` always forced last
// (destroying fallthrough), and with GNU case ranges collapsed to their
// start value. This asserts the fix's source-order/multi-statement/case-
// range/non-last-default shape directly on -m output; #1005's smoke case in
// tools/comptime_native_smoke.py is the load-bearing proof that the
// resulting program actually returns the right answer natively.
int main(void) {
    int r = 0;
    int x = 1;
    switch (x) {
        case 1:
            r += 1;
            // falls through -- must reach case 2's statements too
        case 2:
            r += 100;
            break;
        case 3 ... 5:
            r += 1000;
            break;
        default:
            r += 10000;
    }
    return r == 101 ? 42 : r;
}
