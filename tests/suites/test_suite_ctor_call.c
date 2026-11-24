// CCCC_FLAGS: --testing
// Suite: T(args) and Name<T>(args) call the type's `new` method, on the VM and
// under -c=native.

#include <cccc/derive.h>

@ctor typedef struct {
    int x;
    int y;
} Point;

@ctor typedef struct {
    int w @ default(80);
    int h @ default(24);
    int hits @ctor_skip;
} Window;

@template(T) @ctor struct Cell {
    T v;
};

@template(T) T pick_max(T a, T b) {
    return a > b ? a : b;
}

typedef struct {
    int n;
} Counter;

[[cccc::static_method(Counter, new)]] Counter make_counter(int n) {
    return (Counter){n};
}

[[cccc::test]] void typedef_call(void) {
    Point p = Point(3, 4);
    AssertEq(p.x, 3);
    AssertEq(p.y, 4);
}

[[cccc::test]] void same_as_new(void) {
    Point a = Point(1, 2);
    Point b = Point.new(1, 2);
    AssertEq(a.x, b.x);
    AssertEq(a.y, b.y);
}

[[cccc::test]] void defaults_and_skipped_members(void) {
    Window w = Window();
    AssertEq(w.w, 80);
    AssertEq(w.h, 24);
    AssertEq(w.hits, 0);
}

[[cccc::test]] void template_instance(void) {
    Cell<int> c = Cell<int>(42);
    AssertEq(c.v, 42);
}

[[cccc::test]] void inst_spelling(void) {
    Cell<int> c = @inst(int) Cell(7);
    AssertEq(c.v, 7);
}

[[cccc::test]] void nested_instance(void) {
    Cell<Cell<int>> c = Cell<Cell<int>>(Cell<int>(9));
    AssertEq(c.v.v, 9);
}

[[cccc::test]] void as_argument_and_return(void) {
    AssertEq(pick_max<int>(Cell<int>(5).v, Point(2, 3).y), 5);
}

[[cccc::test]] void user_static_new(void) {
    Counter c = Counter(5);
    AssertEq(c.n, 5);
}

typedef int Num;

[[cccc::test]] void parenthesised_declarator_is_still_a_declaration(void) {
    Num(x);
    x               = 3;
    Num (*fp)(void) = 0;
    AssertEq(x, 3);
    AssertTrue(fp == 0);
}

[[cccc::test]] void function_template_call_is_unchanged(void) {
    AssertEq(pick_max<int>(40, 42), 42);
}
