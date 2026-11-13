#include <stdio.h>

// Derive attributes run on every instance of a template.
@template(T) @equals @compare @to_json @ctor
struct Vec2 {
    T x, y;
    T dot(const struct Vec2 *self, const struct Vec2 *o) { return self->x * o->x + self->y * o->y; }
};

// A method lives in the struct; an operator is a generic function...
@template(T) @inst(T) Vec2 vec2_add(@inst(T) Vec2 a, @inst(T) Vec2 b) {
    return (@inst(T) Vec2){a.x + b.x, a.y + b.y};
}

// ...bound to `+` for one instance.
typedef Vec2@(int) IVec;
[[cccc::operator(+)]] IVec ivec_add(IVec a, IVec b) { return vec2_add@(int)(a, b); }

int main(void) {
    IVec a = Vec2__int_new(3, 4), b = Vec2__int_new(1, 2);
    IVec c = a + b;                             // ivec_add(a, b)

    char buf[64];
    Vec2__int_to_json(&c, buf, sizeof buf);
    puts(buf);                                  // {"x":4,"y":6}

    printf("%d %d\n", a.dot(&b), a < c);        // 11 1
    return c == Vec2__int_new(4, 6) ? 42 : 1;   // == comes from @equals, < from @compare
}
