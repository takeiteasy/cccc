// @serialize_skip, @accessor_skip and @constructor_skip.
#include <cccc/derive.h>
#include <string.h>

@serialize @deserialize struct S {
    int keep;
    int secret @serialize_skip;
    int tail;
};

@generate_accessors struct A {
    int shown;
    int hidden @accessor_skip;
};

@generate_constructor struct C {
    int given;
    int unset @constructor_skip;
    int also;
};

int main(void) {
    struct S s = {1, 99, 3};
    char     buf[sizeof(struct S)];
    memset(buf, 0, sizeof buf);
    if (S_serialize(&s, buf) != (int)sizeof(struct S))
        return 1;
    struct S r = S_deserialize(buf);
    if (r.keep != 1 || r.secret != 0 || r.tail != 3)
        return 2;

    struct A a = {5, 6};
    set_shown(&a, 7);
    if (get_shown(&a) != 7 || a.hidden != 6)
        return 3;

    struct C c = C_create(4, 8);
    if (c.given != 4 || c.unset != 0 || c.also != 8)
        return 4;
    return 42;
}
