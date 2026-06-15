// Tests: multi-declarator typedef for function-pointer-returning-array type;
// asm("symbol") labels on each declarator; compound-literal computed argument.
// Godbolt: exit 0, stdout "BYE, WORLD!"
// CCCC: compile error — "expected ','" on asm-label declarator list.
#include <stdio.h>

long typedef*(_)(long*(__),...), (___)[];

int main() {

  char* s = "Hello, world.";

  _(_) asm("open"), (__) asm("pwrite");
  __(_((___){
    [!0] = 121 *  3081 * 17807L * 18119 + 3,
    [!1] =  59 * 69829 *  1933L * 53359 * 17203 }, 1), (___){
    [!0] =  12 *    97 *  1873L,
    [!1] = 238 *  1511 *  5749L * 13709 * 209263 + 4 }, 12, s);

  puts(s);

}
