// Tests: array of empty unions initialized to {}; element accessed via
// subscript and passed to printf as a vararg (%d).
// Godbolt: exit 0, stdout "Let's count: 3 2 1 0"
// CCCC: exit 0, stdout "Let's count: 3 346980400 2 1" — var[42] gives
// garbage instead of 0; empty union not zero-initialised in vararg position.
#include <stdio.h>
#include <fcntl.h>

union {} var[100] = {};

int main() {
  printf("Let's count: %d %d %d %d\n", 3, var[42], 2, 1);
}
