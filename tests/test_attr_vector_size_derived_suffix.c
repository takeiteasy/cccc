// vector_size after a pointer, array or function declarator vectorizes the
// innermost scalar, as in gcc: a pointer to a vector, an array of vectors, a
// function returning a vector.

#define VS __attribute__((vector_size(16)))

int *gq VS;
int     gb[2] VS;
int (*gap)[2] VS;
typedef int *PV VS;
typedef int     AV[3] VS;
struct S {
    int *p VS;
    int    a[2] VS;
};

int mk(void) VS;
VS int mk(void) {
    VS int v = {40, 2, 0, 0};
    return v;
}

int main(void) {
    int *q   VS;
    int      b[2] VS;
    int **pp VS;
    int (*fp)(void) VS = mk;
    int x VS           = {1, 2, 3, 4};

    if (sizeof *gq != 16 || sizeof gq != sizeof(void *))
        return 1;
    if (sizeof gb != 32 || sizeof gb[0] != 16)
        return 2;
    if (sizeof **gap != 16 || sizeof *gap != 32)
        return 3;
    if (sizeof *(PV)0 != 16 || sizeof(AV) != 48)
        return 4;
    if (sizeof(struct S) != 8 + 32 + 8 || sizeof((struct S *)0)->p[0] != 16 ||
        sizeof((struct S *)0)->a[0] != 16)
        return 5;
    if (sizeof *q != 16 || sizeof b != 32 || sizeof **pp != 16)
        return 6;
    if (sizeof(fp()) != 16 || sizeof(mk()) != 16 || mk()[0] != 40 ||
        fp()[1] != 2)
        return 7;

    b[1] = x;
    int *scratch VS;
    scratch = &b[1];
    if (b[1][2] != 3 || (*scratch)[3] != 4)
        return 8;
    return 42;
}
