// Test C23 auto type inference

typedef struct { int x; int y; } Point;

// File-scope auto (constant initializer)
auto global_i = 42;
auto global_d = 3.14;

void void_fn(void) {}

int main(void) {
    // Scalar type deduction
    auto i = 5;
    if (sizeof(i) != sizeof(int)) return 1;
    if (i != 5) return 2;

    auto d = 3.14;
    if (sizeof(d) != sizeof(double)) return 3;

    auto f = 3.14f;
    if (sizeof(f) != sizeof(float)) return 4;

    // const stripping: const int -> int
    const int ci = 10;
    auto a = ci;
    a = 20; // must be assignable (not const)
    if (a != 20) return 5;

    // String literal: const char[] decays and strips const -> char *
    auto s = "hello";
    s = "world"; // must be assignable
    if (s[0] != 'w') return 6;

    // Array decay: int[3] -> int *
    int arr[3] = {1, 2, 3};
    auto p = arr;
    if (*p != 1) return 7;
    if (*(p + 2) != 3) return 8;

    // Function decay: void(void) -> void (*)(void)
    auto fn = void_fn;
    fn(); // must be callable
    if (fn != void_fn) return 9;

    // Pointer declarator: auto *q = &i -> int *
    int x = 99;
    auto *q = &x;
    if (*q != 99) return 10;
    *q = 100;
    if (x != 100) return 11;

    // Double pointer
    auto **pp = &q;
    if (**pp != 100) return 12;

    // Struct via compound literal
    auto pt = (Point){3, 7};
    if (pt.x != 3 || pt.y != 7) return 13;

    // Multiple declarators on one line, each with own type
    auto ai = 1, bd = 2.0;
    if (sizeof(ai) != sizeof(int)) return 14;
    if (sizeof(bd) != sizeof(double)) return 15;

    // File-scope globals
    if (global_i != 42) return 16;
    if (sizeof(global_i) != sizeof(int)) return 17;

    return 42;
}
