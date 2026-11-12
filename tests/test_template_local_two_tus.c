// CCCC_FLAGS: tests/fixtures/template_local_tu_b.c
// An instance over a block-scope type is private to its file, and a tagged and
// a typedef-named `P` are different instances; a file-scope type's instance is
// still shared.
#include "fixtures/template_local.h"

struct P {
    char c[3];
};

int (*b_file_scope(void))(void);
int (*b_block_scope(void))(void);
int (*b_typedef_p(void))(void);
int (*b_shared(void))(void);

int main(void) {
    struct S {
        int x;
    };
    int (*block_a)(void)  = &@inst(struct S) size_of;
    int (*tagged_p)(void) = &@inst(struct P) size_of;
    int (*shared_a)(void) = &@inst(struct Shared) size_of;

    bool ok = block_a() == 4 && b_block_scope()() == 2 && b_file_scope()() == 8;
    ok      = ok && block_a != b_block_scope() && block_a != b_file_scope();
    ok      = ok && tagged_p() == 3 && b_typedef_p()() == 20 &&
              tagged_p != b_typedef_p();
    ok      = ok && shared_a() == 16 && shared_a == b_shared();
    return ok ? 42 : 1;
}
