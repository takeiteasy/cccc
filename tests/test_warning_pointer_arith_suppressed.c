// JCC_FLAGS: -Wpointer-arith -Wno-pointer-arith
// JCC_REJECT_STDERR: warning:

int main(void) {
    char buf[8] = {1,2,3,4,5,6,7,8};
    void *p = buf;
    void *q = p + 4;
    return *((char *)q) == 5 ? 42 : 0;
}
