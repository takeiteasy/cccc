// The macro standard library attributes accept `typedef` declarations as well
// as `struct X {...};` / `enum X {...};`.
#include <string.h>

@serialize @deserialize @generate_constructor @generate_getters("Pt")
    @generate_setters("Pt") typedef struct {
    int x;
    int y;
} Pt;

@enum_to_string @enum_from_string typedef enum { RED, GREEN, BLUE } Color;

@serialize @deserialize typedef struct Named {
    long a;
    char b;
} Alias;

int main(void) {
    Pt p = Pt_create(3, 4);
    if (Pt_get_x(&p) != 3 || Pt_get_y(&p) != 4)
        return 1;
    Pt_set_y(&p, 9);
    char buf[sizeof(Pt)];
    if (Pt_serialize(&p, buf) != (int)sizeof(Pt))
        return 2;
    Pt q = Pt_deserialize(buf);
    if (q.x != 3 || q.y != 9)
        return 3;

    if (strcmp(Color_to_string(GREEN), "GREEN") != 0)
        return 4;
    if (Color_from_string("BLUE") != BLUE)
        return 5;

    Alias a = {7, 'z'};
    char  abuf[sizeof(Alias)];
    Alias_serialize(&a, abuf);
    Alias b = Alias_deserialize(abuf);
    if (b.a != 7 || b.b != 'z')
        return 6;
    return 42;
}
