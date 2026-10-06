// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// Indexing a row that does not exist (one past the last row) is reported even
// though the column index is in range.
int main(int argc, char **argv) {
    (void)argv;
    int m[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] = 0;
    int row   = 3 + argc - 1;
    m[row][0] = 1;
    return 42;
}
