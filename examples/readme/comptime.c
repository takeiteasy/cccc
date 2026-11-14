#include <stdio.h>

@template(T) struct Vec2 { T x, y; };

// A call-site macro: the call is replaced by the returned template.
@comptime Node *square(Node *x) { return Quote("($1) * ($1)", x); }

// A comptime function in a global initializer folds to a constant.
@comptime Node *pow2(Node *k) { return Quote("1 << ($1)", k); }

// A file-scope macro that instantiates the template and emits a function over
// the instance.
@comptime Node *emit_vec2_int(void) {
    InstantiateType("Vec2", "int");
    return Quote("{ int vec2_dot(struct Vec2_int a, struct Vec2_int b) {"
                 "      return a.x * b.x + a.y * b.y; } }");
}
emit_vec2_int();

int TABLE_SIZE = pow2(8);                  // 256, computed at compile time

int main(void) {
    Vec2@(int) a = { 3, 4 };
    Vec2@(int) b = { square(2), 5 };       // square(2) -> (2) * (2)
    printf("%d %d\n", TABLE_SIZE, vec2_dot(a, b));   // 256 32
    return 0;
}

// Discovered and run by `cccc --testing comptime.c`.
@test void dot_product(void) {
    Vec2@(int) a = { 3, 4 }, b = { square(2), 5 };
    AssertEq(vec2_dot(a, b), 32);
}

// Run by `cccc --build comptime.c`.
@build_target BuildTarget *demo(Builder *ctx) {
    BuildTarget *t = CcccExecutable(ctx, "demo");
    AddSource(t, "comptime.c");
    return t;
}
