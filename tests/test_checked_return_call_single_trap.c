// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

static int buf[4];
static int *[[cccc::single]] one(void) {
    return (int *[[cccc::single, cccc::assume]])buf;
}
int main(void) {
    int *q = one();
    return q[1] + 42;
}
