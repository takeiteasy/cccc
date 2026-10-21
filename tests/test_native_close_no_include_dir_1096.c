// A bodiless declaration from a bundled header round-trips under -c=native.
#include <fcntl.h>
extern int close(int fd);
int main(void) {
    int f = 0;
    if (f)
        f = close(1);
    return 42;
}
