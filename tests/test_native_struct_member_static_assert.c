// A static assertion in a struct body over a host-owned layout compiles under -c=native.
#include <sys/mount.h>

union U {
    char c;
    _Static_assert(sizeof(struct statfs) >= 8, "statfs too small");
    int  i;
};

int main(void) {
    union U u;
    u.i = 42;
    struct L {
        char c;
        _Static_assert(sizeof(struct statfs) >= 8, "statfs too small (local)");
        int  d;
    } l = {0, 0};
    return u.i + l.d;
}
