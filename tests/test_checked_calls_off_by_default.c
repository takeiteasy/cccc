// CCCC_REJECT_STDERR: checked-calls
// Checked regions: without -Wchecked-calls a call across the boundary is
// silent.

int raw(int *p) {
    return p[0];
}

[[cccc::checked]]
int f(void) {
    int *[[cccc::array, cccc::count(1)]] a = (int _Checked[1]){42};
    return raw(a);
}

int main(void) {
    return f();
}
