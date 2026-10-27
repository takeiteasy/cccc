// Without -O, __OPTIMIZE__ is not defined.
#ifdef __OPTIMIZE__
int main(void) {
    return 1;
}
#else
int main(void) {
    return 42;
}
#endif
