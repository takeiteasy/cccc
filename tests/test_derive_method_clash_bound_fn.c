// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method 'equals' is already defined for this type
#include <cccc/derive.h>

@equals
struct S {
    int a;
};

[[cccc::method(struct S, equals)]]
int same(struct S *self, struct S *o) {
    return self->a == o->a;
}

int main(void) {
    return 0;
}
