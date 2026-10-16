// CCCC_EXPECT_STDERR: unknown attribute 'no_such_handler' ignored \[-Wunknown-attributes\]
// CCCC_REJECT_STDERR: -Wattributes
// An @name with no handler is dropped with a default warning; the
// __attribute__ spelling is only reported under -Wattributes.
struct P {
    int x @no_such_handler;
    int y __attribute__((no_such_handler));
};

int main(void) {
    struct P p = {40, 2};
    return p.x + p.y;
}
