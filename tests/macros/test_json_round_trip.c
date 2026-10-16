// @to_json / @from_json round trip over every supported member kind.

#include <cccc/derive.h>
#include <cccc/json.h>
#include <string.h>

enum Mode { OFF, ON = 5 };

typedef struct {
    int x;
    int y;
} Point;

@to_json
@from_json
typedef struct {
    int           id;
    unsigned char small;
    long long     big;
    unsigned long ubig;
    double        ratio;
    float         f;
    _Bool         flag;
    enum Mode     mode;
    char          name[16];
    int           scores[3];
    Point         origin;
    Point         path[2];
    struct {
        int inner;
    } anon;
} Record;

int main(void) {
    Record a = {.id    = -7,
                .small = 200,
                .big   = -1234567890123LL,
                .ubig  = 4000000000UL,
                .ratio = 0.1,
                .f     = 2.5f,
                .flag  = 1,
                .mode  = ON,
                .name  = "he said \"hi\"\n",
                .scores = {1, 2, 3},
                .origin = {3, 4},
                .path   = {{5, 6}, {7, 8}},
                .anon   = {9}};

    char   buf[512];
    size_t n = Record_to_json(&a, buf, sizeof buf);
    if (n != strlen(buf))
        return 1;

    Record b;
    memset(&b, 0, sizeof b);
    if (Record_from_json(&b, buf))
        return 2;
    if (b.id != -7 || b.small != 200 || b.big != -1234567890123LL ||
        b.ubig != 4000000000UL || b.ratio != 0.1 || b.f != 2.5f || !b.flag ||
        b.mode != ON || strcmp(b.name, a.name) || b.scores[0] != 1 ||
        b.scores[1] != 2 || b.scores[2] != 3 || b.origin.x != 3 ||
        b.origin.y != 4 || b.path[0].x != 5 || b.path[0].y != 6 ||
        b.path[1].x != 7 || b.path[1].y != 8 || b.anon.inner != 9)
        return 3;
    return 42;
}
