// EXPECT_COMPILE_ERROR
// GNU __builtin_convertvector (tracker #715): same-domain conversions (both
// arms integer, or both arms float) have no opcode yet -- only int32<->
// float32 and int64<->float64 cross-domain pairs are supported.

typedef int          v4si __attribute__((vector_size(16)));
typedef unsigned int v4su __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};
    v4su b = __builtin_convertvector(
        a, v4su); // error: same-domain (int -> int) not supported
    return b[0] == 1u ? 42 : 1;
}
