// @from_json: accepted input, skipped members and every rejection.

#include <cccc/derive.h>
#include <cccc/json.h>
#include <string.h>

@from_json
typedef struct {
    int           n;
    unsigned char u;
    double        d;
    _Bool         b;
    char          s[6];
    int           v[2];
} Doc;

static int load(Doc *d, const char *json) {
    memset(d, 0, sizeof *d);
    return Doc_from_json(d, json);
}

int main(void) {
    Doc d;

    if (load(&d, " { \"n\" : -12 , \"u\":255,\"d\":1.5e2,\"b\":false,"
                 "\"s\":\"ab\\u00e9\",\"v\":[1, 2] } "))
        return 1;
    if (d.n != -12 || d.u != 255 || d.d != 150.0 || d.b ||
        strcmp(d.s, "ab\xc3\xa9") || d.v[0] != 1 || d.v[1] != 2)
        return 2;

    // Members the struct lacks are skipped, whatever they hold, in any order.
    if (load(&d, "{\"extra\":{\"a\":[1,{\"b\":null}],\"c\":\"x\\\"y\"},"
                 "\"v\":[3,4],\"s\":\"\",\"b\":true,\"d\":0,\"u\":0,\"n\":9,"
                 "\"more\":[true,false,null,-1.5]}"))
        return 3;
    if (d.n != 9 || !d.b || d.v[1] != 4)
        return 4;

    // Surrogate pair.
    if (load(&d, "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\\ud83d\\ude00\","
                 "\"v\":[0,0]}") ||
        strcmp(d.s, "\xf0\x9f\x98\x80"))
        return 5;

    static const char *const bad[] = {
        "",                                                    // empty
        "{",                                                   // truncated
        "[]",                                                  // not an object
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\"}",     // missing v
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"toolong\",\"v\":[0,0]}",
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0]}",
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0,0]}",
        "{\"n\":1,\"u\":256,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0]}",
        "{\"n\":1,\"u\":-1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0]}",
        "{\"n\":1.5,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0]}",
        "{\"n\":99999999999,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0]}",
        "{\"n\":\"1\",\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0]}",
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":1,\"s\":\"\",\"v\":[0,0]}",
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\\ud800\",\"v\":[0,0]}",
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0],}",
        "{\"n\":1,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0]} x",
        "{\"n\":01,\"u\":1,\"d\":1,\"b\":true,\"s\":\"\",\"v\":[0,0]}",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (!load(&d, bad[i]))
            return 10 + (int)i;
    return 42;
}
