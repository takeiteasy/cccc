// a block capture of a by-value struct larger than 8 bytes (24-byte struct,
// three long members) round-trips VM 42 -> native 42
struct BigS {
    long a;
    long b;
    long c;
};
int main(void) {
    struct BigS t;
    t.a            = 10;
    t.b            = 20;
    t.c            = 12;
    int (^b)(void) = ^{
      return (int)(t.a + t.b + t.c);
    };
    return b();
}
