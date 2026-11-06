// CCCC_FLAGS: --std=c17
// CCCC_NATIVE_SKIP: the -c=native block-call cast ignores an unprototyped block's arguments (#1946)

int main(void) {
    int (^add)() = ^(int a, int b) {
      return a + b;
    };
    return add(40, 2);
}
