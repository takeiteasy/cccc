// @to_json output shape, snprintf-style truncation and the name prefix.

#include <cccc/derive.h>
#include <cccc/json.h>
#include <string.h>

@to_json
struct Point {
    int x;
    int y;
};

@to_json("app")
typedef struct {
    char  label[8];
    _Bool on;
    int   tags[2];
    float nan;
} Item;

int main(void) {
    struct Point p = {-3, 4};
    char         buf[64];

    size_t n = Point_to_json(&p, buf, sizeof buf);
    if (n != 14 || strcmp(buf, "{\"x\":-3,\"y\":4}"))
        return 1;

    // Too small: the full length is still returned, the buffer is cut and
    // terminated.
    char small[6];
    memset(small, 'z', sizeof small);
    if (Point_to_json(&p, small, sizeof small) != 14 || strcmp(small, "{\"x\":"))
        return 2;

    // cap 0 only measures.
    if (Point_to_json(&p, NULL, 0) != 14)
        return 3;

    Item it = {.label = "a\tb", .on = 1, .tags = {7, 8}};
    it.nan  = 0.0f / 0.0f;
    app_Item_to_json(&it, buf, sizeof buf);
    if (strcmp(buf, "{\"label\":\"a\\tb\",\"on\":true,\"tags\":[7,8],\"nan\":null}"))
        return 4;

    // A const struct is accepted.
    const struct Point cp = {1, 2};
    Point_to_json(&cp, buf, sizeof buf);
    if (strcmp(buf, "{\"x\":1,\"y\":2}"))
        return 5;
    return 42;
}
