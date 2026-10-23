// CCCC_FLAGS: -fopenmp
int main(void) {
    int n = 0;
#pragma omp parallel for reduction(+ : n)
    for (int i = 0; i < 8; i++) {
        switch (i % 2) {
            case 0:
                n += 1;
                break;
            default:
                break;
        }
        for (int j = 0; j < 4; j++) {
            if (j == 1)
                break;
        }
    }
    return n == 4 ? 42 : 1;
}
