// Test codegen switch lowering for dense ranges and sparse case sets

int dense_range(int x) {
    switch (x) {
        case 0 ... 299:
            return x;
        default:
            return -1;
    }
}

int sparse_many(int x) {
    switch (x) {
        case -1000: return 1;
        case -500: return 2;
        case -25: return 3;
        case 0: return 4;
        case 17: return 5;
        case 42: return 6;
        case 99: return 7;
        case 1234: return 8;
        case 5000: return 9;
        case 9000: return 10;
        case 12000: return 11;
        case 20000: return 12;
        default: return -1;
    }
}

int sparse_no_default(int x) {
    int y = 33;
    switch (x) {
        case 10: y = 10; break;
        case 1000: y = 42; break;
        case 100000: y = 99; break;
    }
    return y;
}

int main(void) {
    if (dense_range(0) != 0) return 1;
    if (dense_range(255) != 255) return 2;
    if (dense_range(299) != 299) return 3;
    if (dense_range(300) != -1) return 4;
    if (dense_range(-1) != -1) return 5;

    if (sparse_many(-1000) != 1) return 6;
    if (sparse_many(42) != 6) return 7;
    if (sparse_many(20000) != 12) return 8;
    if (sparse_many(41) != -1) return 9;

    if (sparse_no_default(1000) != 42) return 10;
    if (sparse_no_default(11) != 33) return 11;

    return 42;
}
