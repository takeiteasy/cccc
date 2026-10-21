// -c=native, pointer-to-VLA-row subtraction divides by the row's runtime size
// (both directions) and a multi-dimensional VLA brace initializer round-trips
// as real C
int main(void) {
    int n = 2, m = 3;
    int v[n][m] = {{1, 2, 3}, {4, 5, 6}};
    int total = v[0][0] + v[0][1] + v[0][2] + v[1][0] + v[1][1] + v[1][2]; // 21

    long d1   = &v[1] - &v[0];
    long d0   = &v[0] - &v[1];
    total     = total - total + (total == 21 && d1 == 1 && d0 == -1 ? 42 : 1);

    return total;
}
