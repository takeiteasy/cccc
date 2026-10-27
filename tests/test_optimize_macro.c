// CCCC_FLAGS: -O1
// -O1 and above define __OPTIMIZE__.
#ifdef __OPTIMIZE__
int main(void) {
    return 42;
}
#else
int main(void) {
    return 1;
}
#endif
