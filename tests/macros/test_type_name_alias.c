// TypeName() reports a tag or typedef name, the spelling of a plain builtin
// scalar, and NULL for anything else, whatever variables were declared with
// the type.
#include <stdint.h>
#include <string.h>

typedef struct {
    int a;
    int b;
} Point;
typedef enum { RED, GREEN } Color;
typedef struct Tagged {
    int a;
} TagAlias;
typedef long   mylong;
typedef mylong mylong2;
typedef int   *IntPtr;

@template(T) struct Box {
    T value;
};

int use(Point p, Color c, TagAlias t, mylong m, mylong2 m2, IntPtr ip, int i,
        long l, unsigned long ul, long long ll) {
    return p.a + c + t.a + (int)m + (int)m2 + (ip ? *ip : 0) + i + (int)l +
           (int)ul + (int)ll;
}

@comptime Node *check(void) {
    if (strcmp(TypeName(GetType("Point")), "Point") != 0)
        return MakeIntLiteral(1);
    if (strcmp(TypeName(GetType("Color")), "Color") != 0)
        return MakeIntLiteral(2);
    if (strcmp(TypeName(GetType("TagAlias")), "Tagged") != 0)
        return MakeIntLiteral(3);
    if (strcmp(TypeName(GetType("mylong")), "mylong") != 0)
        return MakeIntLiteral(4);
    if (strcmp(TypeName(GetType("mylong2")), "mylong2") != 0)
        return MakeIntLiteral(5);
    if (strcmp(TypeName(GetType("IntPtr")), "IntPtr") != 0)
        return MakeIntLiteral(6);
    if (strcmp(TypeName(GetType("intmax_t")), "intmax_t") != 0)
        return MakeIntLiteral(7);
    if (strcmp(TypeName(GetType("int")), "int") != 0)
        return MakeIntLiteral(8);
    if (strcmp(TypeName(GetType("long")), "long") != 0)
        return MakeIntLiteral(9);
    if (strcmp(TypeName(GetType("unsigned long")), "unsigned long") != 0)
        return MakeIntLiteral(10);
    if (strcmp(TypeName(GetType("long long")), "long long") != 0)
        return MakeIntLiteral(11);
    if (TypeName(MakePointer(GetType("int"))) != NULL)
        return MakeIntLiteral(12);
    return MakeIntLiteral(0);
}

int main(void) {
    Box @(Point) box = {{1, 2}};
    int n            = 0;
    return 42 + check() + box.value.a - 1 +
           use((Point){0, 0}, RED, (TagAlias){0}, 0, 0, &n, 0, 0, 0, 0);
}
