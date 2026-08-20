// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #752: byte-granular tracking (#653) applies to a global's member
// subobjects too, not just its base address -- a store through one member
// of a file-scope struct stamps only that member's byte range in
// vm->data_shadow, so reinterpreting a *different* member's bytes as an
// unrelated type must still be caught.
struct S {
    int   a;
    float b;
};

struct S g;

int main(void) {
    g.a       = 5;             // stamps g.a's range as int
    int *p    = (int *)&g.b;   // g.b's range: never stamped as int
    *p        = 3;             // stamps g.b's range as int instead
    float *fp = (float *)&g.b; // same address, reinterpreted
    return (int)*fp;           // load as float: mismatches stamped int
}
