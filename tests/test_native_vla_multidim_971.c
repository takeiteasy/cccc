// -c=native, a multi-dimensional VLA (2-D subscript, a decayed row pointer, and
// 3-D subscript) round-trips as real C and no longer SIGSEGVs in the VM
int main(void) {
    int n = 2, m = 3;
    int v[n][m];
    v[0][0]     = 1;
    v[0][1]     = 2;
    v[0][2]     = 3;
    v[1][0]     = 4;
    v[1][1]     = 5;
    v[1][2]     = 6;
    int  total  = v[0][0] + v[0][1] + v[0][2] + v[1][0] + v[1][1] + v[1][2];

    int *row    = v[1];
    row[2]      = 42;
    total      += v[1][2] - 6; // now 42 instead of 6

    int k       = 4;
    int w[n][m][k];
    w[1][2][3] = 42;
    total      = total - total + w[1][2][3]; // isolate the 3-D result

    return total;
}
