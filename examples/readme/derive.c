#include <stdio.h>
#include <string.h>
#include <cccc/derive.h>
#include <cccc/json.h>

@enum_to_string
@enum_from_string
typedef enum { RED, GREEN, BLUE } Color;

@to_json
@from_json
@equals
@ctor
typedef struct {
    int id;
    char name[16];
    Color favourite;
} User;

int main(void) {
    User u = User_new(7, "ann", GREEN);

    char buf[128];
    User_to_json(&u, buf, sizeof buf);
    puts(buf);                                // {"id":7,"name":"ann","favourite":1}

    User v;
    if (User_from_json(&v, buf) != 0 || !User_equals(&u, &v))
        return 1;

    printf("%s\n", Color_to_string(v.favourite));   // GREEN
    return 42;
}
