#include "template_local.h"

struct S {
    char x[8];
};
typedef struct {
    int q[5];
} P;

int (*b_file_scope(void))(void) {
    return &@inst(struct S) size_of;
}

int (*b_block_scope(void))(void) {
    struct S {
        char z[2];
    };
    return &@inst(struct S) size_of;
}

int (*b_typedef_p(void))(void) {
    return &@inst(P) size_of;
}

int (*b_shared(void))(void) {
    return &@inst(struct Shared) size_of;
}
