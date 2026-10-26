// CCCC_FLAGS: --checked-pointers
// A dynamic cast of an assume cast within the inner claim passes.

int main(void) {
    int buf[5] = {0};
    int *raw   = buf;
    int *[[cccc::array, cccc::count(5)]] q =
        (int *[[cccc::array, cccc::count(5), cccc::dynamic]])(
            int *[[cccc::array, cccc::count(5), cccc::assume]])raw;
    (void)q;
    return 42;
}
