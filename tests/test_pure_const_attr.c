// Test __attribute__((pure)) and __attribute__((const)) parsing
// Verifies GCC-style and C23-style syntax are accepted without errors.

// GCC-style declarations
__attribute__((pure)) int pure_decl(int x);
__attribute__((__pure__)) int pure_us_decl(int x);
__attribute__((const)) int const_decl(int x);
__attribute__((__const__)) int const_us_decl(int x);

// GCC-style definitions
__attribute__((pure)) int pure_fn(int x) { return x * 2; }
__attribute__((const)) int const_fn(int x) { return x + 1; }

// Both pure and const work on static functions
static __attribute__((pure)) int static_pure(int x) { return x - 1; }
static __attribute__((const)) int static_const(int x) { return x * x; }

// C23-style [[gnu::pure]] and [[gnu::const]]
[[gnu::pure]] int gnu_pure(int x) { return x + 10; }
[[gnu::const]] int gnu_const(int x) { return x * 3; }

int main(void) {
    if (pure_fn(5) != 10) return 1;
    if (const_fn(5) != 6) return 2;
    if (static_pure(5) != 4) return 3;
    if (static_const(5) != 25) return 4;
    if (gnu_pure(5) != 15) return 5;
    if (gnu_const(5) != 15) return 6;
    return 42;
}
