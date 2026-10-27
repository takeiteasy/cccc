// Expected return: 42
// `__typeof` (glibc's <sys/cdefs.h>) spells `typeof`, as `__typeof__` does.
int main(void) {
    int             a = 40;
    __typeof(a)     b = 2;
    __typeof__(a)   c = 0;
    __typeof(a + b) d = a + b + c;
    return d;
}
