// EXPECT_COMPILE_ERROR
// GNU __builtin_convertvector (tracker #715): source and target vectors
// must have the same lane count.

typedef int   v4si __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};
    v8hi b = __builtin_convertvector(a, v8hi); // error: lane count 4 != 8
    return b[0] == 1 ? 42 : 1;
}
