// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: const char \*name
// CCCC_EXPECT_STDOUT: struct LObj \*car
// CCCC_REJECT_STDOUT: va_list
// CCCC_HOST_CC
// -m output for an anonymous struct used as a union member prints the real
// body, never va_list.
#include <stdarg.h>
struct LObj {
    int tag;
    union {
        struct {
            const char *name;
        } atom;
        struct {
            struct LObj *car, *cdr;
        } pair;
    } as;
};
struct LObj g;
int main(void) {
    return 42;
}
