// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: close\(
// CCCC_NO_INCLUDE_DIR
// -m output keeps the bodiless declaration when headers come from the embedded
// table.
#include <fcntl.h>
extern int close(int fd);
int main(void) {
    int f = 0;
    if (f)
        f = close(1);
    return 42;
}
