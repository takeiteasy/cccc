// CCCC_FLAGS: --std=c23 -Wincompatible-pointer-types
// CCCC_EXPECT_STDERR: incompatible pointer types
typedef long long i64;
typedef long      L;
int               h(i64);
int h(long long);
int k(long);
int k(L);
unsigned long long u(unsigned long long int);
unsigned long long u(unsigned long long);

enum E : long long { A = 1 };

#define T(e)                                                                   \
    _Generic(e,                                                                \
        long: 1,                                                               \
        unsigned long: 2,                                                      \
        long long: 3,                                                          \
        unsigned long long: 4,                                                 \
        default: 9)

int main(void) {
    long               l   = 0;
    long long          ll  = 0;
    unsigned long      ul  = 0;
    unsigned long long ull = 0;
    long              *p   = &ll;
    (void)p;
    if (T(l) != 1 || T(ul) != 2 || T(ll) != 3 || T(ull) != 4)
        return 1;
    if (T(1L) != 1 || T(1UL) != 2 || T(1LL) != 3 || T(1ULL) != 4)
        return 2;
    if (T(l + ll) != 3 || T(ll + l) != 3 || T(ul + ll) != 4 ||
        T(ll + ul) != 4 || T(ull + l) != 4)
        return 3;
    if (T(A) != 3)
        return 4;
    return 42;
}
