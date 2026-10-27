// CCCC_FLAGS: --checked-pointers
// A struct argument named by a return bound is evaluated once.

struct Len {
    int  n;
    long pad[4];
};

static int buf[8] = {40};
static int calls;
static struct Len mk(void) {
    calls++;
    struct Len l = {8, {0}};
    return l;
}
static int *[[cccc::array, cccc::count(l.n)]] head(struct Len l) {
    return (int *[[cccc::array, cccc::count(l.n), cccc::assume]])buf;
}

int main(void) {
    int x = head(mk())[7] + head(mk())[0];
    return x + 2 + (calls == 2 ? 0 : 100);
}
