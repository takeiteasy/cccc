// A dynamic cast from a call whose return bound names a struct parameter is
// accepted without the flag.

struct Len {
    int n;
};

static int buf[8];
static int *[[cccc::array, cccc::count(l.n)]] head(struct Len l) {
    return (int *[[cccc::array, cccc::count(l.n), cccc::assume]])buf;
}
int main(void) {
    struct Len l = {8};
    int *[[cccc::array, cccc::count(4)]] p =
        (int *[[cccc::array, cccc::count(4), cccc::dynamic]])head(l);
    return p[0] + 42;
}
