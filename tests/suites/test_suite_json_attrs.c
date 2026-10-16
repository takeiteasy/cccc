// CCCC_FLAGS: --testing
// Suite: @to_json / @from_json, run on the VM and under -c=native.

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
    double        ratio;
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

@to_json("app")
@from_json("app")
struct Pair {
    int a;
    int b;
};

[[cccc::test]]
void test_round_trip(void) {
    Record a = {.id     = -7,
                .small  = 200,
                .big    = -1234567890123LL,
                .ratio  = 0.1,
                .flag   = 1,
                .mode   = ON,
                .name   = "he said \"hi\"\n",
                .scores = {1, 2, 3},
                .origin = {3, 4},
                .path   = {{5, 6}, {7, 8}},
                .anon   = {9}};
    char   buf[512];
    size_t n = Record_to_json(&a, buf, sizeof buf);
    AssertEq((int)n, (int)strlen(buf));

    Record b;
    memset(&b, 0, sizeof b);
    AssertEq(Record_from_json(&b, buf), 0);
    AssertEq(b.id, -7);
    AssertEq(b.small, 200);
    AssertEq(b.big == -1234567890123LL, 1);
    AssertEq(b.ratio == 0.1, 1);
    AssertEq(b.flag, 1);
    AssertEq(b.mode, ON);
    AssertEq(strcmp(b.name, a.name), 0);
    AssertEq(b.scores[2], 3);
    AssertEq(b.origin.y, 4);
    AssertEq(b.path[1].x, 7);
    AssertEq(b.anon.inner, 9);
}

[[cccc::test]]
void test_output_and_truncation(void) {
    struct Pair p = {-3, 4};
    char        buf[32];
    AssertEq((int)app_Pair_to_json(&p, buf, sizeof buf), 14);
    AssertEq(strcmp(buf, "{\"a\":-3,\"b\":4}"), 0);

    char small[6];
    AssertEq((int)app_Pair_to_json(&p, small, sizeof small), 14);
    AssertEq(strcmp(small, "{\"a\":"), 0);
}

[[cccc::test]]
void test_from_json_rejections(void) {
    struct Pair p;
    AssertEq(app_Pair_from_json(&p, "{\"a\":1,\"b\":2,\"c\":[null]}"), 0);
    AssertEq(p.b, 2);
    AssertEq(app_Pair_from_json(&p, "{\"a\":1}") != 0, 1);
    AssertEq(app_Pair_from_json(&p, "{\"a\":1,\"b\":\"2\"}") != 0, 1);
    AssertEq(app_Pair_from_json(&p, "{\"a\":1,\"b\":2} x") != 0, 1);
    AssertEq(app_Pair_from_json(&p, "{\"a\":1,\"b\":99999999999}") != 0, 1);
}
