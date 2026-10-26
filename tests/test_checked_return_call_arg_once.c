// CCCC_FLAGS: --checked-pointers

// An argument the bounds name is evaluated once, even with a side effect.
static int buf[8];
static int calls;
static int next(void) {
    return ++calls + 1;
}
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *q = head(next());
    q[1]   = 40;
    int  i = 2;
    int *r = head(i++);
    r[0]   = 2;
    return calls == 1 && i == 3 ? q[1] + r[0] : 1;
}
