#define EVAL0(...) __VA_ARGS__
#define EVAL1(...) EVAL0(EVAL0(EVAL0(__VA_ARGS__)))
#define EVAL2(...) EVAL1(EVAL1(EVAL1(__VA_ARGS__)))
#define EVAL3(...) EVAL2(EVAL2(EVAL2(__VA_ARGS__)))
#define EVAL4(...) EVAL3(EVAL3(EVAL3(__VA_ARGS__)))
#define EVAL(...)  EVAL4(EVAL4(EVAL4(__VA_ARGS__)))

#define MAP_END(...)
#define MAP_OUT
#define MAP_GET_END2()             0, MAP_END
#define MAP_GET_END1(...)          MAP_GET_END2
#define MAP_GET_END(...)           MAP_GET_END1
#define MAP_NEXT0(test, next, ...) next MAP_OUT
#define MAP_NEXT1(test, next)      MAP_NEXT0(test, next, 0)
#define MAP_NEXT(test, next)       MAP_NEXT1(MAP_GET_END test, next)
#define MAP0(f, x, peek, ...) f(x) MAP_NEXT(peek, MAP1)(f, peek, __VA_ARGS__)
#define MAP1(f, x, peek, ...) f(x) MAP_NEXT(peek, MAP0)(f, peek, __VA_ARGS__)
#define MAP(f, ...)            EVAL(MAP1(f, __VA_ARGS__, ()()(), 0))

static int sum;
#define ADD(x) sum += (x);

int main(void) {
    sum = 0;
    MAP(ADD, 1, 2, 3, 4, 5)
    if (sum != 15) return 1;
    sum = 0;
    MAP(ADD, 10, 20, 30)
    if (sum != 60) return 2;
    return 42;
}
