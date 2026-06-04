// JCC_FLAGS: -Wpointer-arith
// JCC_EXPECT_STDERR: \[-Wpointer-arith\]

int main(void) {
    char buf[8] = {1,2,3,4,5,6,7,8};
    void *p = buf;
    // void* arithmetic: GNU extension, warns with -Wpointer-arith
    void *q = p + 4;
    return *((char *)q) == 5 ? 42 : 0;
}
