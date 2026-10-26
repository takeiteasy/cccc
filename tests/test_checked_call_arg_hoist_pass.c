// CCCC_FLAGS: --checked-pointers
// A side-effecting sibling argument runs exactly once, ahead of the call, and
// the bounds are checked against the value the callee receives.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);
void sink_after(int n, int *[[cccc::array, cccc::count(n)]] p);

static int calls = 0;
static int seen  = 0;
static int f(void) {
    calls++;
    return 2;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};

    sink(small, f());
    if (calls != 1 || seen != 2)
        return 1;

    int i = 2;
    sink(small, i++);
    if (i != 3 || seen != 2)
        return 1;

    sink_after(f(), small);
    if (calls != 2 || seen != 2)
        return 1;

    return 42;
}

void sink(int *p, int n) {
    (void)p;
    seen = n;
}

void sink_after(int n, int *p) {
    (void)p;
    seen = n;
}
