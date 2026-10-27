// CCCC_FLAGS: --checked-pointers
static int calls;
struct Len {
    int  n;
    long pad[3];
};
static struct Len mk(void) {
    calls++;
    struct Len l = {2, {0}};
    return l;
}
static int sink(int *[[cccc::array, cccc::count(l.n)]] p, struct Len l) {
    return p[0] + p[1];
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){40, 2};
    return sink(small, mk()) + (calls == 1 ? 0 : 100);
}
