// -c=native, a 1-D VLA (plain, combined `n=..,v[n]` declaration, and one
// re-declared per loop iteration) round-trips as a real C VLA
int main(void) {
    int n = 4;
    int v[n];
    v[0]       = 10;
    v[n - 1]   = 21;
    int total  = v[0] + v[3];

    int m      = 3, w[m];
    w[0]       = 5;
    total     += w[0];

    for (int i = 1; i <= 3; i++) {
        int len = i;
        int loopvla[len];
        loopvla[0]  = i;
        total      += loopvla[0];
    }

    return total;
}
