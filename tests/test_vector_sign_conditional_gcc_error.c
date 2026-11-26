// EXPECT_COMPILE_ERROR
// gcc rejects `?:` arms that differ only in vector signedness.

typedef int      i4 __attribute__((vector_size(16)));
typedef unsigned u4 __attribute__((vector_size(16)));

int main(void) {
    i4 a = {1, 2, 3, 4};
    u4 b = {1, 2, 3, 4};
    i4 c = 1 ? a : b;
    return c[0];
}
