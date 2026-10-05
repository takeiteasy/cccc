// The reserved `__gnu__::` scope and `__name__` attribute spellings are
// equivalent to `gnu::name`, including in __has_c_attribute.
// CCCC_FLAGS: -Wattributes -Werror=attributes
#include <stddef.h>

#if !__has_c_attribute(gnu::cold) || !__has_c_attribute(__gnu__::aligned) ||   \
    !__has_c_attribute(__gnu__::__aligned__) ||                                \
    __has_c_attribute(gnu::bogus) || __has_c_attribute(__gnu__::bogus)
#error "__has_c_attribute does not recognise the gnu scope"
#endif

struct A {
    char c;
    int  x [[__gnu__::aligned(64)]];
};
struct B {
    char c;
    int  x [[gnu::__aligned__(32)]];
};
struct [[gnu::__packed__]] P {
    char c;
    int  i;
};
struct [[__gnu__::packed]] Q {
    char c;
    int  i;
};

static_assert(offsetof(struct A, x) == 64);
static_assert(offsetof(struct B, x) == 32);
static_assert(sizeof(struct P) == 5);
static_assert(sizeof(struct Q) == 5);

typedef int v4i [[__gnu__::vector_size(16)]];

[[__gnu__::cold]] void cold_fn(void);
void cold_fn(void) {}

[[__gnu__::__noinline__]] static int one(void) {
    return 1;
}

[[__nodiscard__]] static int forty(void) {
    return 40;
}

int main(void) {
    v4i v = {forty(), 0, 0, 0};
    cold_fn();
    return v[0] + one() + 1;
}
