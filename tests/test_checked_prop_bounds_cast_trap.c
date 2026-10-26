// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// An unchecked local initialised from a bounds cast carries its claimed bounds.

int main(void) {
    int buf[5] = {0};
    int *raw   = buf;
    int *r     = (int *[[cccc::array, cccc::count(5), cccc::assume]])raw;
    return r[7] + 42;
}
