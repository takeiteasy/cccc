// CCCC_FLAGS: --std=c17

typedef struct {
    int x, y;
} Pair;

int main(void) {
    int (^add)() = ^(int a, int b) {
      return a + b;
    };
    if (add(40, 2) != 42)
        return 1;

    char   c = 40;
    float  d = 2.0f;
    double (^mix)() = ^(int a, double b) {
      return a + b;
    };
    if ((int)mix(c, d) != 42)
        return 2;

    int arr[3] = {40, 1, 1};
    int (^sum)() = ^(int *p) {
      return p[0] + p[1] + p[2];
    };
    if (sum(arr) != 42)
        return 3;

    Pair p = {40, 2};
    int (^pair)() = ^(Pair q) {
      return q.x + q.y;
    };
    if (pair(p) != 42)
        return 4;
    return 42;
}
