// EXPECT_RUNTIME_ERROR JCC_FLAGS: --ffi-deny=strlen --ffi-errors-fatal
#include <string.h>

int main(void) {
    return (int)strlen("fatal");
}
