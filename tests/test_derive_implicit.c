// A derive attribute loads <cccc/derive.h>, the JSON ones <cccc/json.h> and
// @from_string/@text <cccc/text.h>, without an #include.
#include <string.h>

@to_string @from_string typedef enum { RED, GREEN, BLUE } Color;

@to_json @from_json @equals typedef struct {
    int   id;
    Color favourite;
} User;

int main(void) {
    User u = {7, GREEN};
    char buf[64];
    User_to_json(&u, buf, sizeof buf);
    User v;
    if (strcmp(buf, "{\"id\":7,\"favourite\":1}") || User_from_json(&v, buf) ||
        !User_equals(&u, &v) || Color_from_string("BLUE") != BLUE)
        return 1;
    return 42;
}
