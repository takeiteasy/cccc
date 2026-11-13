// CCCC_FLAGS:
// A file-scope generator returning Quote("{ ... }") with an operator or method
// attribute binds it once, however many times the call is parsed.
struct P { int x; };

@comptime Node *gen(void) {
    return Quote("{ [[cccc::operator(+)]] int padd(struct P a, struct P b) { return a.x + b.x; }"
                 "  [[cccc::method(struct P, get)]] int pget(struct P a) { return a.x; } }");
}
gen();

int main(void) {
    struct P a = {10}, b = {11};
    return (a + b) + a.get() + b.get();
}
