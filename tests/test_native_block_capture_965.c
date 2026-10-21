// -c=native, a block literal with a by-value capture and a block call
// round-trip as real C
int main(void) {
    int a           = 21;
    int (^add)(int) = ^(int x) {
      return x + a;
    };
    int r = add(21);
    return r == 42 ? 42 : 1;
}
