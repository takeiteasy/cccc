// CCCC_FLAGS: -Wpedantic
// CCCC_REJECT_STDERR: constructor call is a CCCC extension
@ctor typedef struct {
    int x;
} P;

int main(void) {
    P p = P.new(42);
    return p.x;
}
