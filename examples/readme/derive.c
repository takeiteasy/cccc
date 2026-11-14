#include <stdio.h>

// Derive attributes run on every instance of a template, and the functions
// they generate are methods of the struct.
@template(T)
@equals
@compare
@to_json
@ctor
struct Vec2 {
    T x, y;
    T dot(const struct Vec2 *self, const struct Vec2 *o) { return self->x * o->x + self->y * o->y; }
};

// A method lives in the struct; an operator is a function template, deduced
// from its operands.
@template(T)
[[cccc::operator(+)]]
@inst(T) Vec2 vec2_add(@inst(T) Vec2 a, @inst(T) Vec2 b) {
    return (@inst(T) Vec2){a.x + b.x, a.y + b.y};
}

int main(void) {
    Vec2@(int) a = Vec2@(int).new(3, 4), b = Vec2@(int).new(1, 2);
    Vec2@(int) c = a + b;                       // vec2_add_int(a, b)

    char buf[64];
    c.to_json(buf, sizeof buf);                 // also callable as Vec2_int_to_json
    puts(buf);                                  // {"x":4,"y":6}

    printf("%d %d\n", a.dot(&b), a < c);        // 11 1
    return c.equals(&(Vec2@(int)){4, 6}) ? 42 : 1;  // == and < come from @equals, @compare
}
