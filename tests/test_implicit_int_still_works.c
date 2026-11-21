// CCCC_FLAGS: --std=c89
// An implicit-int declaration is not an unknown type name.

static counter;
const limit = 42;
get() {
    return limit;
}

int main(void) {
    return counter + get();
}
