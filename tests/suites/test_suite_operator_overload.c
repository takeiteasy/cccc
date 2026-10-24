// CCCC_FLAGS: --testing
// Suite: [[cccc::operator(...)]] binds a built-in operator to a function for
// struct operands.
#pragma cccc suite begin "operator_overload"

typedef struct {
    int x, y;
} Vec2;

typedef struct {
    int n;
} Counter;

[[cccc::operator(+)]]
Vec2 vec2_add(Vec2 a, Vec2 b) {
    return (Vec2){a.x + b.x, a.y + b.y};
}

[[cccc::operator(-)]]
Vec2 vec2_sub(const Vec2 *a, const Vec2 *b) {
    return (Vec2){a->x - b->x, a->y - b->y};
}

[[cccc::operator(*)]]
Vec2 vec2_scale(Vec2 a, int k) {
    return (Vec2){a.x * k, a.y * k};
}

[[cccc::operator(*)]]
Vec2 scale_vec2(int k, Vec2 a) {
    return (Vec2){a.x * k, a.y * k};
}

[[cccc::operator(/)]]
Vec2 vec2_div(Vec2 a, int k) {
    return (Vec2){a.x / k, a.y / k};
}

[[cccc::operator(%)]]
Vec2 vec2_mod(Vec2 a, int k) {
    return (Vec2){a.x % k, a.y % k};
}

[[cccc::operator(==)]]
bool vec2_eq(Vec2 a, Vec2 b) {
    return a.x == b.x && a.y == b.y;
}

[[cccc::operator(!=)]]
bool vec2_ne(Vec2 a, Vec2 b) {
    return !(a.x == b.x && a.y == b.y);
}

[[cccc::operator(<)]]
int counter_lt(Counter a, Counter b) {
    return a.n < b.n;
}

[[cccc::operator(<=)]]
int counter_le(Counter a, Counter b) {
    return a.n <= b.n;
}

[[cccc::operator(>)]]
int counter_gt(Counter a, Counter b) {
    return a.n > b.n;
}

[[cccc::operator(>=)]]
int counter_ge(Counter a, Counter b) {
    return a.n >= b.n;
}

[[cccc::test(return = 42)]]
int test_operator_arithmetic(void) {
    Vec2 a = {10, 20}, b = {1, 2};
    Vec2 s = a + b;
    Vec2 d = a - b;
    if (s.x != 11 || s.y != 22)
        return 1;
    if (d.x != 9 || d.y != 18)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_operator_mixed_operand_types(void) {
    Vec2 a = {2, 3};
    Vec2 l = a * 4;
    Vec2 r = 4 * a;
    Vec2 q = l / 2;
    Vec2 m = l % 5;
    if (l.x != 8 || l.y != 12 || r.x != 8 || r.y != 12)
        return 1;
    if (q.x != 4 || q.y != 6)
        return 2;
    if (m.x != 3 || m.y != 2)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_operator_chain_and_rvalue_operands(void) {
    Vec2 a = {1, 1}, b = {2, 2}, c = {3, 3};
    Vec2 s = a + b + c;
    Vec2 t = (a + b) * 2 + c;
    if (s.x != 6 || s.y != 6)
        return 1;
    if (t.x != 9 || t.y != 9)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_operator_comparisons(void) {
    Vec2    a = {1, 2}, b = {1, 2}, c = {3, 4};
    Counter lo = {1}, hi = {2};
    if (!(a == b) || a != b || !(a != c) || a == c)
        return 1;
    if (!(lo < hi) || hi < lo || !(lo <= lo))
        return 2;
    if (!(hi > lo) || lo > hi || !(hi >= hi) || lo >= hi)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_operator_compound_assignment(void) {
    Vec2 v[2]  = {{1, 1}, {10, 10}};
    Vec2 d     = {1, 2};
    int  i     = 0;
    v[i++]    += d;
    v[1]      += v[0];
    if (i != 1)
        return 1;
    if (v[0].x != 2 || v[0].y != 3)
        return 2;
    if (v[1].x != 12 || v[1].y != 13)
        return 3;
    v[0] = v[0] * 3;
    if (v[0].x != 6 || v[0].y != 9)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_operator_in_conditions_and_arguments(void) {
    Vec2 a = {1, 2}, b = {3, 4};
    Vec2 sum = vec2_add(a, b);
    if (sum == a + b)
        return 42;
    return 1;
}

typedef struct {
    int n;
} Cell;

[[cccc::operator(==)]]
bool cell_same(const Cell *a, const Cell *b) {
    return a->n == b->n;
}

[[cccc::operator(<)]]
bool cell_bump_less(Cell *a, const Cell *b) {
    a->n += 100;
    return a->n < b->n;
}

[[cccc::operator(+)]]
Cell cell_add(const Cell *a, const Cell *b) {
    return (Cell){a->n + b->n};
}

static Cell make_cell(int n) {
    return (Cell){n};
}

[[cccc::test(return = 42)]]
int test_operator_rvalue_operand_takes_a_temporary(void) {
    Cell a = {1}, b = {3};
    int  flag = 1;
    if (!(make_cell(1) == a))
        return 1;
    if (!(a + b == make_cell(4)))
        return 2;
    if (!((flag ? a : b) == a))
        return 3;
    if (!((0, a) == a))
        return 4;
    if (!((Cell)a == a))
        return 5;
    if (!(a == (Cell){1}))
        return 6;
    return 42;
}

[[cccc::test(return = 42)]]
int test_operator_rvalue_to_mutable_pointer_discards_writes(void) {
    Cell a = {1};
    if (!((Cell)a < make_cell(500)))
        return 1;
    if (make_cell(5) < make_cell(50))
        return 2;
    if (a.n != 1)
        return 3;
    return 42;
}

[[cccc::operator(%)]]
static inline Vec2 mod_vec2_inline(int k, Vec2 a) {
    return (Vec2){k % a.x, k % a.y};
}

[[cccc::test(return = 42)]]
int test_operator_static_inline(void) {
    Vec2 a = {4, 3};
    Vec2 c = 9 % a;
    if (c.x != 1 || c.y != 0)
        return 1;
    return 42;
}

typedef struct {
    int v;
} Tile;

[[cccc::operator(==)]]
bool tile_eq(const Tile *a, const Tile *b) {
    return a->v == b->v;
}

typedef struct {
    int v;
} Notch;

[[cccc::operator(==)]]
bool notch_eq(const Notch *a, const Notch *b) {
    return a->v == b->v;
}

[[cccc::operator(!=)]]
bool notch_ne(const Notch *a, const Notch *b) {
    return a->v + 100 != b->v;
}

[[cccc::test]] void test_ne_falls_back_to_negated_eq(void) {
    Tile a = {1}, b = {1}, c = {2};
    AssertEq(!!(a != b), 0);
    AssertEq(!!(a != c), 1);
}

[[cccc::test]] void test_bound_ne_wins_over_fallback(void) {
    Notch a = {1}, b = {1};
    AssertEq(!!(a == b), 1);
    AssertEq(!!(a != b), 1);
}

typedef struct {
    unsigned bits;
} Mask;

[[cccc::operator(&)]]
Mask mask_and(Mask a, Mask b) {
    return (Mask){a.bits & b.bits};
}

[[cccc::operator(|)]]
Mask mask_or(const Mask *a, const Mask *b) {
    return (Mask){a->bits | b->bits};
}

[[cccc::operator(^)]]
Mask mask_xor(Mask a, unsigned k) {
    return (Mask){a.bits ^ k};
}

[[cccc::operator(<<)]]
Mask mask_shl(Mask a, int n) {
    return (Mask){a.bits << n};
}

[[cccc::operator(>>)]]
Mask mask_shr(Mask a, int n) {
    return (Mask){a.bits >> n};
}

[[cccc::test(return = 42)]]
int test_operator_bitwise_and_shift(void) {
    Mask a = {0xf0}, b = {0x3c};
    if ((a & b).bits != 0x30)
        return 1;
    if ((a | b).bits != 0xfc)
        return 2;
    if ((a ^ 0xffu).bits != 0x0f)
        return 3;
    if ((a << 4).bits != 0xf00 || (a >> 4).bits != 0xf)
        return 4;
    if ((a & b | a >> 4).bits != 0x3f)
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_operator_bitwise_compound_assignment(void) {
    Mask m[2]  = {{0xff}, {0x0f}};
    int  i     = 0;
    m[i++]    &= m[1];
    if (i != 1 || m[0].bits != 0x0f)
        return 1;
    m[0] |= m[1];
    m[1] ^= 0x01u;
    if (m[1].bits != 0x0e)
        return 2;
    m[1] <<= 2;
    m[1] >>= 1;
    if (m[1].bits != 0x1c)
        return 3;
    return 42;
}

typedef struct {
    int n;
} Fx;

[[cccc::operator(*)]]
Fx fx_mul(Fx a, Fx b) {
    return (Fx){a.n * b.n};
}

[[cccc::operator(/)]]
Fx fx_div(Fx a, int k) {
    return (Fx){a.n / k};
}

[[cccc::operator(%)]]
Fx fx_mod(Fx a, int k) {
    return (Fx){a.n % k};
}

[[cccc::test(return = 42)]]
int test_operator_mul_div_mod_compound_assignment(void) {
    Fx  v[2]  = {{6}, {7}};
    int i     = 0;
    v[i++]   *= v[1];
    if (i != 1 || v[0].n != 42)
        return 1;
    v[0] /= 2;
    v[0] %= 5;
    if (v[0].n != 1)
        return 2;
    return 42;
}

typedef struct Flags {
    unsigned f;
    Flags operator &(Flags a, Flags b) {
        return (Flags){a.f & b.f};
    }
    Flags operator | (const Flags *a, const Flags *b) {
        return (Flags){a->f | b->f};
    }
    Flags operator ^ (Flags a, Flags b) {
        return (Flags){a.f ^ b.f};
    }
    Flags operator << (Flags a, int n) {
        return (Flags){a.f << n};
    }
    Flags operator >> (Flags a, int n) {
        return (Flags){a.f >> n};
    }
} Flags;

[[cccc::test(return = 42)]]
int test_operator_bitwise_in_struct(void) {
    Flags a = {0x3f}, b = {0x2a};
    Flags c  = (a & b) | b;
    c        = (c << 2) >> 2;
    c       ^= (Flags){0};
    if (Flags_operator_xor(c, (Flags){0}).f != 0x2a)
        return 1;
    return c.f == 0x2a ? 42 : 2;
}

// clang-format off
typedef struct {
    int v;
} Rank;

typedef struct {
    int v;
} Strict;

[[cccc::operator(<=>)]]
int rank_cmp(const Rank *a, const Rank *b) {
    return a->v - b->v;
}

[[cccc::operator(<=>)]]
int strict_cmp(const Strict *a, const Strict *b) {
    return a->v - b->v;
}

[[cccc::operator(<)]]
bool strict_lt(const Strict *a, const Strict *b) {
    return a->v > b->v;
}

[[cccc::test]] void test_relational_falls_back_to_three_way(void) {
    Rank a = {1}, b = {2};
    AssertEq(!!(a < b), 1);
    AssertEq(!!(a <= b), 1);
    AssertEq(!!(a > b), 0);
    AssertEq(!!(a >= b), 0);
    AssertEq(!!(b >= a), 1);
}

[[cccc::test]] void test_bound_lt_wins_over_three_way(void) {
    Strict a = {1}, b = {2};
    AssertEq(!!(a < b), 0);
    AssertEq(!!(b < a), 1);
    AssertEq(!!(a <= b), 1);
}

[[cccc::test]] void test_three_way_on_structs_calls_the_bound_function(void) {
    Rank a = {1}, b = {5};
    AssertEq(a <=> b, -4);
    AssertEq(b <=> a, 4);
    AssertEq(a <=> a, 0);
}

[[cccc::test]] void test_three_way_on_scalars_is_minus_one_zero_one(void) {
    AssertEq(1 <=> 2, -1);
    AssertEq(2 <=> 1, 1);
    AssertEq(3 <=> 3, 0);
    AssertEq(-1 <=> 1u, 1);
    AssertEq(1.5 <=> 2, -1);
    double nan = 0.0 / (volatile double){0.0};
    AssertEq(nan <=> 1.0, 0);
    int arr[2];
    AssertEq(&arr[0] <=> &arr[1], -1);
}

static int evals;
static int tick(int v) {
    evals++;
    return v;
}

[[cccc::test]] void test_three_way_evaluates_operands_once(void) {
    evals = 0;
    AssertEq(tick(1) <=> tick(2), -1);
    AssertEq(evals, 2);
}

[[cccc::test]] void test_three_way_binds_looser_than_shift_tighter_than_relational(void) {
    AssertEq(1 << 1 <=> 3, -1);
    AssertEq(!!(1 <=> 2 < 0), 1);
    AssertEq(!!(0 < 1 <=> 0), 1);
}
// clang-format on

#pragma cccc suite end
