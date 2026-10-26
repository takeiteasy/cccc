// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A program that redeclares a libc function with its own bounds replaces the
// built-in ones: n = 8 fits a[12], but the program's byte_count(n * 2) does
// not.
#include <unistd.h>

ssize_t read(int    fd, void *[[cccc::array, cccc::byte_count(n * 2)]] buf,
             size_t n);

int main(void) {
    char a[12];
    int  fds[2];
    pipe(fds);
    write(fds[1], "abcdefgh", 8);
    volatile size_t n = 8;
    read(fds[0], a, n);
    return 0;
}
