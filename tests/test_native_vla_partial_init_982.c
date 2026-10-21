// -c=native, a partial multi-dimensional VLA brace initializer zero-fills its
// omitted row
int main(void) {
    int n = 2, m = 2;
    int v[n][m] = {{1, 2}}; // second row omitted
    int total =
        v[0][0] + v[0][1] * 10 + (v[1][0] == 0 && v[1][1] == 0 ? 0 : 100);
    return total == 21 ? 42 : 1;
}
