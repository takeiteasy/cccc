// Postfix ++/-- on _Bool yields the value before the store, which saturates to
// 0/1; prefix yields the new value.
struct S {
    _Bool f;
    _Bool g : 1;
};

static _Bool glob = 1;

int main(void) {
    _Bool b = 1;
    int   r = b++;
    if (r != 1 || b != 1)
        return 1;
    _Bool c = 0;
    r       = c--;
    if (r != 0 || c != 1)
        return 2;
    _Bool d = 0;
    r       = d++;
    if (r != 0 || d != 1)
        return 3;
    _Bool e = 1;
    r       = e--;
    if (r != 1 || e != 0)
        return 4;
    _Bool p = 0;
    if (++p != 1 || p != 1)
        return 5;
    _Bool q = 1;
    if (--q != 0 || q != 0)
        return 6;

    struct S s = {1, 0};
    if (s.f++ != 1 || s.f != 1)
        return 7;
    if (s.g++ != 0 || s.g != 1)
        return 8;
    if (glob++ != 1 || glob != 1)
        return 9;

    _Bool arr[2] = {0, 1};
    int   i      = 0;
    if (arr[i++]++ != 0 || arr[0] != 1 || i != 1)
        return 10;
    return 42;
}
