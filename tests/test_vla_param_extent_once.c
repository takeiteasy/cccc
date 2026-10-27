// A parameter extent with side effects is evaluated once per call, through
// a prototype and a function pointer too.

int calls;
int rows(void) {
    return ++calls, 4;
}
int pick(int (*a)[rows()], int i);
int main(void) {
    int b[2][4]                        = {};
    b[1][2]                            = 41;
    int (*fp)(int (*a)[rows()], int i) = pick;
    int r                              = fp(b, 1) + pick(b, 1);
    return r == 82 ? 42 : r;
}
int pick(int (*a)[rows()], int i) {
    int (*q)[4] = a;
    return q[i][2];
}
