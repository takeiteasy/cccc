#include <cccc/derive.h>

@compare struct S {
    int a;
    int cache @equals_skip;
    int b;
};

int main(void) {
    struct S x = {1, 100, 2};
    struct S y = {1, -5, 2};
    if (S_compare(&x, &y) != 0)
        return 1;
    y.b = 3;
    return S_compare(&x, &y) < 0 ? 42 : 2;
}
