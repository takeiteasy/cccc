// CCCC_FLAGS: -Wattributes
// CCCC_REJECT_STDERR: warning
// CCCC_EXPECT_STDOUT: ^ext 1 neon 1 poly 1$
#include <stdio.h>
typedef float f4 __attribute__((ext_vector_type(4)));
int main(void) {
    int ext = 0, neon = 0, poly = 0;
#if __has_attribute(ext_vector_type)
    ext = 1;
#endif
#if __has_attribute(neon_vector_type)
    neon = 1;
#endif
#if __has_attribute(neon_polyvector_type)
    poly = 1;
#endif
    printf("ext %d neon %d poly %d\n", ext, neon, poly);
    f4 v = {1, 2, 3, 4};
    return v.w == 4 ? 42 : 1;
}
