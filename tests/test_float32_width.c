// Test float uses 32-bit load/store and rounding semantics

float gf = 1.25f;
float ga[3] = { 1.0f, 2.0f, 39.0f };

struct S {
    char c;
    float f;
    int i;
};

static float add_one(float x) {
    return x + 1.0f;
}

static int check_param(float x, float y) {
    float z = x + y;
    return z == 42.0f;
}

int main() {
    struct S s;
    s.c = 7;
    s.f = 40.75f;
    s.i = 9;

    float local = 16777216.0f;
    float rounded = local + 1.0f;
    if (rounded != 16777216.0f)
        return 1;

    float casted = (float)16777217.0;
    if (casted != 16777216.0f)
        return 2;

    gf = gf + 0.75f;
    if (gf != 2.0f)
        return 3;

    ga[1] = ga[0] + ga[2];
    if (ga[1] != 40.0f)
        return 4;

    if (s.c != 7 || s.f != 40.75f || s.i != 9)
        return 5;

    if (add_one(s.f) != 41.75f)
        return 6;

    if (!check_param(20.5f, 21.5f))
        return 7;

    return 42;
}
