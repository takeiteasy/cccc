// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A return bound naming a struct parameter is checked against the struct
// passed.

struct Len {
    int n;
};

static int buf[8];
static int *[[cccc::array, cccc::count(l.n)]] head(struct Len l) {
    return (int *[[cccc::array, cccc::count(l.n), cccc::assume]])buf;
}

int main(void) {
    struct Len l = {2};
    return head(l)[5] + 42;
}
