// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Wpedantic -Werror=pedantic
// CCCC_EXPECT_STDERR: error: 'P\(...\)' as a constructor call is a CCCC extension
// \[-Wpedantic\]
@ctor typedef struct {
    int x;
} P;

int main(void) {
    P p = P(42);
    return p.x;
}
