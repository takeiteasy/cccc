// Enumerators sharing a value produce one case; the first name wins.
#include <string.h>

@enum_to_string @enum_from_string enum Dup { A, B, C = 1, D = -5, E = -5 };

int main(void) {
    if (strcmp(Dup_to_string(A), "A") != 0)
        return 1;
    if (strcmp(Dup_to_string(C), "B") != 0)
        return 2;
    if (strcmp(Dup_to_string(E), "D") != 0)
        return 3;
    if (Dup_from_string("C") != C)
        return 4;
    return 42;
}
