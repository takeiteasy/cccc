// Subscripting a vector rvalue: `(a + b)[i]`, `f()[i]`, `(v4){...}[i]`.
// The vector is spilled to a hidden local, so a runtime index works too.

typedef int   v4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));
v4 mk(void) {
    return (v4){40, 1, 2, 3};
}
int main(void) {
    v4  a = {1, 2, 3, 4}, b = {10, 20, 30, 40};
    int i  = 2;
    f4  fa = {1.5f, 2.5f, 3.5f, 4.5f};
    if ((a + b)[0] != 11 || (a + b)[i] != 33)
        return 1;
    if (mk()[0] != 40 || mk()[i] != 2)
        return 2;
    if (((v4){7, 8, 9, 10})[1] != 8)
        return 3;
    if ((fa * fa)[1] != 6.25f)
        return 4;
    if ((a < b)[3] != -1)
        return 5;
    int s = 0;
    for (int k = 0; k < 4; k++)
        s += (a + b)[k];
    if (s != 110)
        return 7;
    a[1] = 5;
    if (a[1] != 5)
        return 8;
    return 42;
}
