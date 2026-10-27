// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A return bound naming a floating parameter is checked against the value
// passed.

static int buf[8];
static int * [[ cccc::array, cccc::count((int)d) ]] head(double d) {
    return (int * [[ cccc::array, cccc::count((int)d), cccc::assume ]]) buf;
}

int main(void) {
    return head(2.0)[5] + 42;
}
