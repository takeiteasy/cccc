#include <stdio.h>

int test_mixed(int i, float f, int j, float g) {
    if (i != 1)
        return 1;
    if (f != 2.0f)
        return 2;
    if (j != 3)
        return 3;
    if (g != 4.0f)
        return 4;
    return 0;
}

int main() {
    return test_mixed(1, 2.0f, 3, 4.0f);
}
