// CCCC_FLAGS: --std=c23
// Test C23 constexpr object guarantees.
constexpr int MAX_SIZE = 5 + 4;
int purrs[MAX_SIZE] = { 0 };

constexpr struct Limits {
    int min;
    int max;
} limits = { 3, MAX_SIZE };

enum { LIMIT_ENUM = MAX_SIZE };
int global_from_constexpr = MAX_SIZE;

int main() {
    constexpr int N = 10;
    constexpr int local_arr[] = { 1, 2, 3 };

    static_assert(N == 10);
    static_assert(MAX_SIZE == 9);
    static_assert(limits.max == 9);
    static_assert(LIMIT_ENUM == 9);
    static_assert(__builtin_constant_p(N));

    int local_purrs[N] = { 0 };
    if (sizeof(purrs) != 9 * sizeof(int)) return 1;
    if (sizeof(local_purrs) != 10 * sizeof(int)) return 2;
    if (sizeof(local_arr) != 3 * sizeof(int)) return 3;
    if (global_from_constexpr != 9) return 4;
    return 42;
}
