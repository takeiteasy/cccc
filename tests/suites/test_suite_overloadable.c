// CCCC_FLAGS: --testing
// Suite: overloadable functions, resolved by parameter types.

#define OVL __attribute__((overloadable))

struct S {
    int a;
};

// Exact matches over the arithmetic types.
static int OVL pick(int x) {
    return 1;
}
static int OVL pick(unsigned x) {
    return 2;
}
static int OVL pick(long x) {
    return 3;
}
static int OVL pick(long long x) {
    return 4;
}
static int OVL pick(float x) {
    return 5;
}
static int OVL pick(double x) {
    return 6;
}
static int OVL pick(long double x) {
    return 7;
}
static int OVL pick(const char *x) {
    return 8;
}
static int OVL pick(int *x) {
    return 9;
}
static int OVL pick(struct S x) {
    return 10;
}
static int OVL pick(void) {
    return 11;
}

[[cccc::test]]
void test_exact_matches(void) {
    int       i  = 0;
    long      l  = 0;
    long long ll = 0;
    struct S  s  = {0};
    AssertEq(pick(1), 1);
    AssertEq(pick(1u), 2);
    AssertEq(pick(l), 3);
    AssertEq(pick(ll), 4);
    AssertEq(pick(1.0f), 5);
    AssertEq(pick(1.0), 6);
    AssertEq(pick(1.0L), 7);
    AssertEq(pick("a"), 8);
    AssertEq(pick(&i), 9);
    AssertEq(pick(s), 10);
    AssertEq(pick(), 11);
}

// A promotion beats a conversion.
static int OVL promo(int x) {
    return 1;
}
static int OVL promo(long x) {
    return 2;
}
static int OVL promo(double x) {
    return 3;
}

[[cccc::test]]
void test_promotion_over_conversion(void) {
    short s = 1;
    char  c = 1;
    _Bool b = 1;
    AssertEq(promo(s), 1);
    AssertEq(promo(c), 1);
    AssertEq(promo(b), 1);
    AssertEq(promo(1.0f), 3);
}

// float and double widen to long double by promotion (a C rule).
static int OVL widen(long double x) {
    return 1;
}
static int OVL widen(int x) {
    return 2;
}

[[cccc::test]]
void test_float_to_long_double_is_promotion(void) {
    AssertEq(widen(1.0f), 1);
    AssertEq(widen(1.0), 1);
    AssertEq(widen(1), 2);
}

// Qualification: the more qualified parameter is the worse match.
static int OVL qual(char *x) {
    return 1;
}
static int OVL qual(const char *x) {
    return 2;
}

[[cccc::test]]
void test_qualifier_selects_exact(void) {
    char       c  = 0;
    const char cc = 0;
    AssertEq(qual(&c), 1);
    AssertEq(qual(&cc), 2);
}

// void * accepts a pointer a more specific overload does not.
static int OVL ptrs(void *x) {
    return 1;
}
static int OVL ptrs(int *x) {
    return 2;
}

[[cccc::test]]
void test_void_pointer_fallback(void) {
    int  i = 0;
    char c = 0;
    AssertEq(ptrs(&i), 2);
    AssertEq(ptrs(&c), 1);
    AssertEq(ptrs((void *)0), 1);
}

// An incompatible pointer converts only when nothing else matches.
static int OVL lastresort(int *x) {
    return 1;
}

[[cccc::test]]
void test_incompatible_pointer_last_resort(void) {
    char c = 0;
    AssertEq(lastresort(&c), 1);
}

// A variadic overload loses to any fixed match.
static int OVL va(int a, ...) {
    return 1;
}
static int OVL va(int a, int b) {
    return 2;
}

[[cccc::test]]
void test_ellipsis_ranks_last(void) {
    AssertEq(va(1, 2), 2);
    AssertEq(va(1, 2, 3), 1);
    AssertEq(va(1), 1);
}

// A declaration with a definition later, and recursion through the set.
static int OVL count(int n);
static int OVL count(double d);
static int OVL count(int n) {
    return n ? count(n - 1) + 1 : 0;
}
static int OVL count(double d) {
    return (int)d;
}

[[cccc::test]]
void test_declare_then_define(void) {
    AssertEq(count(4), 4);
    AssertEq(count(7.9), 7);
}

// At most one function of the name may omit the attribute.
int unmarked(int x) {
    return 1;
}
int OVL unmarked(double x) {
    return 2;
}
int OVL unmarked(const char *x) {
    return 3;
}

[[cccc::test]]
void test_one_unmarked_overload(void) {
    AssertEq(unmarked(1), 1);
    AssertEq(unmarked(1.0), 2);
    AssertEq(unmarked("s"), 3);
}

// The unmarked one may come second, and its redeclaration is allowed.
int OVL late(double x);
int late(int x);
int late(int x) {
    return 1;
}
int OVL late(double x) {
    return 2;
}

[[cccc::test]]
void test_unmarked_declared_later(void) {
    AssertEq(late(1), 1);
    AssertEq(late(1.0), 2);
}

// Resolution works where nothing is evaluated.
static double OVL width(int x);
static float OVL width(float x);

[[cccc::test]]
void test_typeof_resolves_without_a_definition(void) {
    __typeof__(width(1))    d = 1.5;
    __typeof__(width(1.0f)) f = 1.5f;
    AssertEq(sizeof(d), 8);
    AssertEq(sizeof(f), 4);
}

// The attribute spellings.
[[clang::overloadable]] static int spelled(int x) {
    return 1;
}
@overloadable static int spelled(double x) {
    return 2;
}
static int __attribute__((__overloadable__)) spelled(const char *x) {
    return 3;
}

[[cccc::test]]
void test_attribute_spellings(void) {
    AssertEq(spelled(1), 1);
    AssertEq(spelled(1.0), 2);
    AssertEq(spelled(""), 3);
}

// A local variable hides the set.
[[cccc::test]]
void test_local_shadows_set(void) {
    int pick = 5;
    AssertEq(pick, 5);
}

// __func__ names the function as written.
static const char *OVL fname(int x) {
    return __func__;
}
static const char *OVL fname(double x) {
    return __func__;
}

[[cccc::test]]
void test_func_name_is_source_name(void) {
    const char *n = fname(1);
    AssertEq(n[0], 'f');
    AssertEq(n[1], 'n');
    AssertEq(n[5], 0);
}

// An unavailable overload is never picked when another matches.
OVL
__attribute__((unavailable("no match")))
static int gate(...);
static int OVL gate(int x) {
    return 1;
}

[[cccc::test]]
void test_unavailable_overload_is_not_chosen(void) {
    AssertEq(gate(3), 1);
}

// Feature tests.
[[cccc::test]]
void test_feature_tests(void) {
#if __has_attribute(overloadable) && __has_c_attribute(clang::overloadable) && \
    __has_extension(attribute_overloadable) &&                                 \
    __has_extension(overloadable_unmarked) && __has_attribute(unavailable)
    AssertTrue(1);
#else
    AssertFail();
#endif
#if __has_c_attribute(overloadable)
    AssertFail();
#endif
}

// The address of a set: the target type picks the member.
static int OVL hop(int x) {
    return 1;
}
static int OVL hop(double x) {
    return 2;
}
static int OVL hop(int x, int y) {
    return 3;
}

static int (*hop_global)(int)        = hop;
static int (*hop_global_amp)(double) = &hop;
static int (*hop_table[])(int, int)  = {hop};
static struct {
    int (*fn)(double);
} hop_holder = {hop};

static int call_with(int (*fn)(double)) {
    return fn(0);
}

static int (*return_hop(void))(int) {
    return hop;
}

[[cccc::test]]
void test_address_in_initializers(void) {
    int (*a)(int)          = hop;
    int (*b)(double)       = &hop;
    int (*c)(int, int)     = (hop);
    int (*const d)(double) = hop;
    AssertEq(a(0), 1);
    AssertEq(b(0), 2);
    AssertEq(c(0, 0), 3);
    AssertEq(d(0), 2);
    AssertEq(hop_global(0), 1);
    AssertEq(hop_global_amp(0), 2);
    AssertEq(hop_table[0](0, 0), 3);
    AssertEq(hop_holder.fn(0), 2);
}

[[cccc::test]]
void test_address_in_assignment_cast_argument_return(void) {
    int (*p)(int);
    p = hop;
    AssertEq(p(0), 1);
    p = return_hop();
    AssertEq(p(0), 1);
    int (*q)(double) = (int (*)(double))hop;
    AssertEq(q(0), 2);
    AssertEq(call_with(hop), 2);
    int (*r)(const int) = hop;
    AssertEq(r(0), 1);
}

// A parameter that is itself a function pointer: the set's address picks the
// call, as the other function-pointer overload cannot take it.
static int OVL via(int (*fn)(int)) {
    return fn(0) + 10;
}
static int OVL via(float x) {
    return 20;
}

[[cccc::test]]
void test_address_as_overloaded_argument(void) {
    AssertEq(via(hop), 11);
    AssertEq(via(1.0f), 20);
}

// The unmarked function of a name is a member too.
static int mixed(int x) {
    return 1;
}
static int OVL mixed(double x) {
    return 2;
}

[[cccc::test]]
void test_address_with_unmarked_member(void) {
    int (*a)(int)    = mixed;
    int (*b)(double) = mixed;
    AssertEq(a(0), 1);
    AssertEq(b(0), 2);
}

// A parameter with a @default is optional, and a keyed argument picks its
// parameter by name.
static int OVL pad(int w, int h @ default(10)) {
    return w * 100 + h;
}
static int OVL pad(double scale, int k @ default(1), int m @ default(2)) {
    return (int)scale * 1000 + k * 10 + m;
}

[[cccc::test]]
void test_default_fills_omitted_argument(void) {
    AssertEq(pad(1), 110);
    AssertEq(pad(1, 5), 105);
    AssertEq(pad(2.0), 2012);
    AssertEq(pad(2.0, 3), 2032);
    AssertEq(pad(2.0, 3, 4), 2034);
}

[[cccc::test]]
void test_keyed_arguments_pick_by_name(void) {
    AssertEq(pad(.w = 3), 310);
    AssertEq(pad(.h = 7, .w = 3), 307);
    AssertEq(pad(.scale = 4.0), 4012);
    AssertEq(pad(.scale = 4.0, .m = 9), 4019);
    AssertEq(pad(5, .h = 6), 506);
    AssertEq(pad(5.0, .m = 8), 5018);
}

// A default can name an earlier parameter; the declaration carries it and the
// definition inherits it.
static int OVL span(int lo, int hi @ default(lo * 2));
static int OVL span(char *s);
static int OVL span(int lo, int hi) {
    return hi - lo;
}
static int OVL span(char *s) {
    return 0;
}

[[cccc::test]]
void test_default_names_earlier_parameter(void) {
    AssertEq(span(10), 10);
    AssertEq(span(.lo = 4), 4);
    AssertEq(span(.hi = 9, .lo = 1), 8);
    AssertEq(span("x"), 0);
}

// The unmarked function of a name can take defaults too.
static int plain(int a, int b @ default(3)) {
    return a + b;
}
static int OVL plain(double x) {
    return -1;
}

[[cccc::test]]
void test_default_on_unmarked_member(void) {
    AssertEq(plain(1), 4);
    AssertEq(plain(1, 1), 2);
    AssertEq(plain(.b = 5, .a = 1), 6);
    AssertEq(plain(1.5), -1);
}

// A keyed value that names a set is resolved against its parameter.
static int OVL apply(int (*fn)(int), int v @ default(0)) {
    return fn(v);
}
static int OVL apply(float unused) {
    return -1;
}
static int OVL twice_of(int x) {
    return x * 2;
}
static int OVL twice_of(double x) {
    return 0;
}

[[cccc::test]]
void test_keyed_argument_takes_set_address(void) {
    AssertEq(apply(.fn = twice_of, .v = 4), 8);
    AssertEq(apply(twice_of), 0);
}
