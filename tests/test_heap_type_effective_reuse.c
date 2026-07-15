// CCCC_FLAGS: --type-checks
// Ticket #651: reusing a heap allocation as a different type is legal C
// (the effective type updates on each store, C11 6.5p6). CHKT3's
// effective-type model must not false-positive here: the char store
// establishes an effective type of char, the later int store re-stamps it
// to int, and the int load then matches.
#include <stdlib.h>

int main(void) {
    char *b = malloc(8);
    *b = 'x';            // store char: stamps effective type = char
    int *p = (int *)b;   // same base address, reinterpreted
    *p = 42;             // store int: re-stamps effective type = int
    int result = *p;     // load int: matches the current effective type
    free(b);
    return result;
}
