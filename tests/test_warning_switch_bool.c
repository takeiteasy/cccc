// CCCC_FLAGS: -Wswitch-bool
// CCCC_EXPECT_STDERR: switch condition has boolean type.*\[-Wswitch-bool\]

#include <stdbool.h>

int f(bool b) {
    switch (b) {
        case true:  return 1;
        case false: return 0;
    }
    return -1;
}

int main(void) {
    (void)f(true);
    return 42;
}
