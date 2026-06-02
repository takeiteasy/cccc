// EXPECT_RUNTIME_ERROR
#include <stdint.h>

int main(void) {
    int (*fn)(void) = (int (*)(void))(intptr_t)12345;
    return fn();
}
