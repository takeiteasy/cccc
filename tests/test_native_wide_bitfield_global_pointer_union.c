// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: a pointer in a union or at a misaligned offset
//
// A pointer that overlaps other members can't be a slot of its own.
int x;

union WithPointer {
    int *p;
    long l;
    _BitInt(256) f : 100;
};

union WithPointer g = {.p = &x};

int main(void) {
    return g.l != 0;
}
