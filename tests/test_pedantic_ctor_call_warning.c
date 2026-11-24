// CCCC_FLAGS: -Wpedantic
// CCCC_EXPECT_STDERR: warning: 'P\(...\)' as a constructor call is a CCCC extension
// \[-Wpedantic\]
@ctor typedef struct {
    int x;
} P;

int main(void) {
    P p = P(42);
    return p.x;
}
