// #815: a GNU case range that overlaps a later (or earlier) case label is
// the range analogue of a duplicate case value and must also be an error.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate \(or overlapping\) case value
int main(void) {
    switch (3) {
    case 1 ... 5: break;
    case 3: break;
    }
    return 42;
}
