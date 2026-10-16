// @generate_getters/_setters/_accessors take an optional string prefix, so
// two structs sharing a field name do not collide.

#include <cccc/derive.h>

@generate_accessors("A") struct A {
    int x;
};

@generate_getters("B") @generate_setters("B") struct B {
    int x;
    int y;
};

@generate_accessors("") struct C {
    int z;
};

int main(void) {
    struct A a = {1};
    struct B b = {2, 3};
    struct C c = {4};
    A_set_x(&a, 10);
    B_set_y(&b, 30);
    set_z(&c, 40);
    if (A_get_x(&a) != 10 || B_get_x(&b) != 2 || B_get_y(&b) != 30)
        return 1;
    if (get_z(&c) != 40)
        return 2;
    return 42;
}
