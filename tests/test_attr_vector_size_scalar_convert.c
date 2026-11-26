// A scalar operand converts to the vector's element type before it splats:
// an int added to a float vector, a double multiplied into one, and a float
// added to an int vector.
typedef float v4f __attribute__((vector_size(16)));
typedef int   v4i __attribute__((vector_size(16)));

int main(void) {
    v4f a = {1, 2, 3, 4};
    v4f b = a + 1;
    v4f c = a * 2.0;
    v4f d = 10 - a;
    v4i e = {1, 2, 3, 4};
    v4i f = e + 2.9f;
    if (b[0] != 2 || b[3] != 5)
        return 1;
    if (c[1] != 4 || c[3] != 8)
        return 2;
    if (d[0] != 9 || d[3] != 6)
        return 3;
    if (f[0] != 3 || f[3] != 6)
        return 4;
    return 42;
}
