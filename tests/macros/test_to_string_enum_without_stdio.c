// The enum form needs no stdio.
#include <cccc/derive.h>

#include <string.h>

@to_string enum Color { RED, GREEN };

int main(void) {
    return strcmp(Color_to_string(GREEN), "GREEN") == 0 ? 42 : 1;
}
