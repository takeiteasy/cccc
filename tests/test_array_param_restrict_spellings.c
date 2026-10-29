// The glibc spellings of restrict are accepted inside an array parameter's
// brackets, as in <regex.h>'s `regmatch_t __pmatch[__restrict_arr N]`.
int f(int a[__restrict 4]) {
    return a[0];
}
int g(int a[__restrict__ 4]) {
    return a[1];
}
int h(int n, int a[static __restrict n]) {
    return a[2];
}

int main(void) {
    int v[4] = {1, 2, 3, 4};
    return f(v) + g(v) + h(4, v) == 6 ? 42 : 1;
}
