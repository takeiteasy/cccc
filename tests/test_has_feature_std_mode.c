// CCCC_FLAGS: --std=c99
int main(void) {
#if !__has_feature(c99)
    return 1;
#endif
#if __has_feature(c11)
    return 2;
#endif
#if __has_feature(c23)
    return 3;
#endif
    return 42;
}
