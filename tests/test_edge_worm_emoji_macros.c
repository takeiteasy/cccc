// CCCC_EXPECT_STDOUT: -42 \+ 5 = -37
// Tests: -~ (right worm, +1) and ~- (left worm, -1) operator chains;
// emoji identifiers as macro names (🪱 🐍) in UTF-8 source.
#include <stdio.h>

int main(void) {
  printf(" 42 + 1 = %d\n", -~42);
  printf(" 42 - 1 = %d\n", ~-42);
  printf(" 42 + 3 = %d\n", -~-~-~42);
  printf(" 42 - 5 = %d\n", ~-~-~-~-~-42);

#define 🪱 -~
#define 🐍 ~-

  printf(" 42 - 3 + 2 = %d\n", 🪱 🪱 🐍 🐍 🐍 42);
  printf("-42 + 5 = %d\n", 🪱 🪱 🪱 🪱 🪱 -42);
}
