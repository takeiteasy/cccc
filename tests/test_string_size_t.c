// Expected return: 42
// <string.h> size arguments and the strlen family's returns are size_t.
#include <string.h>

int main(void) {
    if (-1 < strlen(""))
        return 1;
    if (_Generic(strlen(""), size_t: 0, default: 1) ||
        _Generic(strspn("", ""), size_t: 0, default: 1) ||
        _Generic(strcspn("", ""), size_t: 0, default: 1))
        return 2;
    size_t (*len)(const char *)                = strlen;
    void *(*cpy)(void *, const void *, size_t) = memcpy;
    char buf[4];
    cpy(buf, "ab", 3);
    return len(buf) == 2 ? 42 : 3;
}
