// An #include inside a function body round-trips under -c=native.
static int accumulate_1287(int n) {
    int acc = 0;
#include "fixtures/infunc_include_1287.inc"
    return acc;
}
int main(void) {
    return accumulate_1287(14);
}
