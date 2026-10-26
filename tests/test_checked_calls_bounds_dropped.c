// CCCC_FLAGS: -Wchecked-calls
// CCCC_EXPECT_STDERR: warning: checked pointer passed to unchecked parameter 1 of 'sink_raw'.*\[-Wchecked-calls\]
// CCCC_REJECT_STDERR: call to 'sink_raw' with an unchecked
// Checked regions: a checked pointer passed to a raw parameter warns once per
// argument, and the prototype-level warning is not repeated for that call.

int sink_raw(int *p, int n) {
    return p[0] + p[1] + n - 2;
}

[[cccc::checked]]
int f(void) {
    int *[[cccc::array, cccc::count(2)]] a = (int _Checked[2]){20, 22};
    return sink_raw(a, 2);
}

int main(void) {
    return f();
}
