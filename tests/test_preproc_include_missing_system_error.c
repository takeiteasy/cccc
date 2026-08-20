// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot open file: No such file or directory
#include <missing_system_header_xyz.h>
int main(void) {
    return 0;
}
