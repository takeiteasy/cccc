// vector_size in leading and after-specifier position, GNU and C23 spellings,
// and in cast/compound-literal/sizeof type-names. The vector replaces the
// declaration's base scalar, so `VS int a, *p` makes `p` a pointer to a vector.

#define VS __attribute__((vector_size(16)))

VS int g1, *gp;
int [[gnu::vector_size(16)]] g2;
[[gnu::vector_size(16)]] int         g3 = {40, 2, 0, 0};
[[gnu::vector_size(16)]] typedef int T1;
typedef int [[gnu::vector_size(16)]] T2;

struct S {
    VS int m;
    int [[gnu::vector_size(16)]] n;
};

VS int pass(VS int a) {
    return a;
}

int main(void) {
    if (sizeof g1 != 16 || sizeof *gp != 16 || sizeof g2 != 16 ||
        sizeof g3 != 16)
        return 1;
    if (g3[0] + g3[1] != 42)
        return 2;
    if (sizeof(T1) != 16 || sizeof(T2) != 16 || sizeof(struct S) != 32)
        return 3;

    VS int a, *p;
    int [[gnu::vector_size(16)]] b         = {1, 2, 3, 4};
    [[gnu::vector_size(16)]] int         c = {5, 6, 7, 8}, d = {1, 1, 1, 1};
    __attribute__((vector_size(16))) int e = {1, 1, 1, 1};
    int __attribute__((vector_size(16))) f = {2, 2, 2, 2};
    const VS int                         k = {9, 9, 9, 9};
    if (sizeof a != 16 || sizeof *p != 16 || sizeof p != sizeof(void *))
        return 4;
    if (sizeof b != 16 || sizeof c != 16 || sizeof d != 16 || sizeof e != 16 ||
        sizeof f != 16 || sizeof k != 16)
        return 5;
    if (k[3] != 9 || (c + d)[0] != 6 || (e + f)[2] != 3)
        return 6;
    if (sizeof(pass(a)) != 16 || pass(b)[3] != 4)
        return 7;

    if (sizeof(VS int) != 16 ||
        sizeof(int __attribute__((vector_size(16)))) != 16 ||
        sizeof(int [[gnu::vector_size(16)]]) != 16)
        return 8;
    if (sizeof((VS int){0}) != 16 ||
        sizeof((int [[gnu::vector_size(16)]]){0}) != 16)
        return 9;
    VS int lit = (VS int){40, 2, 0, 0};
    if (lit[0] + lit[1] != 42)
        return 10;
    return 42;
}
