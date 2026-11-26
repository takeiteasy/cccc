// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: warning: unknown attribute 'ext_vector_type' ignored
// CCCC_EXPECT_STDOUT: ^has 0$
#include <stdio.h>
typedef float f4 __attribute__((ext_vector_type(4)));
int main(void) {
#if __has_attribute(ext_vector_type)
    puts("has 1");
#else
    puts("has 0");
#endif
    return 42;
}
