// break and continue inside switch round-trip under -c=native.
int main(void) {
    int total = 0;
    for (int i = 0; i < 6; i++) {
        switch (i) {
            default:
                total += 1;
                break;
            case 1 ... 3:
                total += i;
                continue;
            case 5:
                total += 100;
                break;
        }
        total += 1000;
    }
    // i=0: default, total+=1+1000=1001
    // i=1..3: case range, total += 1+2+3=6, no +1000 (continue)
    // i=4: default, total+=1+1000=1001
    // i=5: case 5, total+=100+1000=1100
    // total = 1001+6+1001+1100 = 3108
    if (total != 3108)
        return total & 0xff;
    int r = 0;
    for (int j = 0; j < 5; j++) {
        if (j == 3)
            break;
        switch (j) {
            case 0:
                r += 1;
            case 1:
                r += 10;
                break;
            case 2:
                r += 100;
        }
    }
    // j=0: falls through 0->1, r=11; j=1: r=21; j=2: r=121; j=3: break
    return r == 121 ? 42 : r;
}
