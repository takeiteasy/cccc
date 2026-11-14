// Accessor functions are named <T>_get_<field>, so two structs sharing a field
// name do not collide; an optional string prefix goes in front.

#include <cccc/derive.h>

@accessors
struct A {
    int x;
};

@getters("lib")
@setters("lib")
struct B {
    int x;
    int y;
};

@accessors("")
struct C {
    int z;
};

int main(void) {
    struct A a = {1};
    struct B b = {2, 3};
    struct C c = {4};
    A_set_x(&a, 10);
    lib_B_set_y(&b, 30);
    C_set_z(&c, 40);
    if (A_get_x(&a) != 10 || lib_B_get_x(&b) != 2 || lib_B_get_y(&b) != 30)
        return 1;
    if (C_get_z(&c) != 40)
        return 2;
    if (a.get_x() != 10 || b.get_y() != 30 || c.get_z() != 40)
        return 3;
    return 42;
}
