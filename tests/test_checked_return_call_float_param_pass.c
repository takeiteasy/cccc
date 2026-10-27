// CCCC_FLAGS: --checked-pointers
static int buf[8] = {42};
static int * [[ cccc::array, cccc::count((int)d) ]] head(double d) {
    return (int * [[ cccc::array, cccc::count((int)d), cccc::assume ]]) buf;
}

int main(void) {
    return head(8.0)[0];
}
