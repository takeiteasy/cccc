// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: \+ -1
// CCCC_HOST_CC: -std=gnu11 -Wunused-value -Werror -Wno-unused-variable
// -m output has no compensating term and the host compiler reports no unused
// value.
struct B {
    int a : 3;
    int c;
};
int main(void) {
    int r            = 0;
    (int){3}         = 4;
    r               += ((int){3} += 1);
    (struct B){0}.c  = 5;
    (struct B){0}.a  = 1;
    _Atomic int  ai  = 0;
    _Atomic int *pa  = &ai;
    ai++;
    ++ai;
    ai--;
    --ai;
    ai += 3;
    (*pa)++;
    int j = ai++;
    int i = 0, k = 0;
    ai++, ai++;
    i++, i++;
    for (i = 0; i < 2; i++, k++)
        ;
    int v = ai++;
    int w = ++ai;
    return (r == 4 && j == 4 && ai == 9 && i == 2 && k == 2 && v == 7 && w == 9)
               ? 42
               : 1;
}
