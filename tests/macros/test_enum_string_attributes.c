// @to_string / @from_string on an enum.

#include <cccc/derive.h>

#include <string.h>

@to_string @from_string enum Color {
    RED,
    GREEN,
    BLUE,
};

int main(void) {
    if (strcmp(Color_to_string(RED), "RED") != 0)
        return 1;
    if (strcmp(Color_to_string(GREEN), "GREEN") != 0)
        return 2;
    if (strcmp(Color_to_string(BLUE), "BLUE") != 0)
        return 3;
    if (strcmp(Color_to_string((enum Color)99), "") != 0)
        return 4;

    if (Color_from_string("RED") != RED)
        return 5;
    if (Color_from_string("GREEN") != GREEN)
        return 6;
    if (Color_from_string("BLUE") != BLUE)
        return 7;
    if (Color_from_string("nope") != -1)
        return 8;

    return 42;
}
