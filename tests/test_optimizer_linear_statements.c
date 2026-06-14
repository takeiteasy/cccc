// CCCC_FLAGS: --optimize=4
// Regression for ticket #428: collect_promotion_candidates and
// restrict_derived_walk recursed into node->next while also recursing
// into children, making O2+ codegen O(2^N) in straight-line statement
// count.  A long sequence of sibling statements must compile quickly.
int main(void) {
    int a = 0;
    if ((0 + 1) == 1) a = a + 1; else a = a - 1;
    if ((1 + 1) == 2) a = a + 1; else a = a - 1;
    if ((2 + 1) == 3) a = a + 1; else a = a - 1;
    if ((3 + 1) == 4) a = a + 1; else a = a - 1;
    if ((4 + 1) == 5) a = a + 1; else a = a - 1;
    if ((5 + 1) == 6) a = a + 1; else a = a - 1;
    if ((6 + 1) == 7) a = a + 1; else a = a - 1;
    if ((7 + 1) == 8) a = a + 1; else a = a - 1;
    if ((8 + 1) == 9) a = a + 1; else a = a - 1;
    if ((9 + 1) == 10) a = a + 1; else a = a - 1;
    if ((10 + 1) == 11) a = a + 1; else a = a - 1;
    if ((11 + 1) == 12) a = a + 1; else a = a - 1;
    if ((12 + 1) == 13) a = a + 1; else a = a - 1;
    if ((13 + 1) == 14) a = a + 1; else a = a - 1;
    if ((14 + 1) == 15) a = a + 1; else a = a - 1;
    if ((15 + 1) == 16) a = a + 1; else a = a - 1;
    if ((16 + 1) == 17) a = a + 1; else a = a - 1;
    if ((17 + 1) == 18) a = a + 1; else a = a - 1;
    if ((18 + 1) == 19) a = a + 1; else a = a - 1;
    if ((19 + 1) == 20) a = a + 1; else a = a - 1;
    if ((20 + 1) == 21) a = a + 1; else a = a - 1;
    if ((21 + 1) == 22) a = a + 1; else a = a - 1;
    if ((22 + 1) == 23) a = a + 1; else a = a - 1;
    if ((23 + 1) == 24) a = a + 1; else a = a - 1;
    if ((24 + 1) == 25) a = a + 1; else a = a - 1;
    if ((25 + 1) == 26) a = a + 1; else a = a - 1;
    if ((26 + 1) == 27) a = a + 1; else a = a - 1;
    if ((27 + 1) == 28) a = a + 1; else a = a - 1;
    if ((28 + 1) == 29) a = a + 1; else a = a - 1;
    if ((29 + 1) == 30) a = a + 1; else a = a - 1;
    if ((30 + 1) == 31) a = a + 1; else a = a - 1;
    if ((31 + 1) == 32) a = a + 1; else a = a - 1;
    if ((32 + 1) == 33) a = a + 1; else a = a - 1;
    if ((33 + 1) == 34) a = a + 1; else a = a - 1;
    if ((34 + 1) == 35) a = a + 1; else a = a - 1;
    if ((35 + 1) == 36) a = a + 1; else a = a - 1;
    if ((36 + 1) == 37) a = a + 1; else a = a - 1;
    if ((37 + 1) == 38) a = a + 1; else a = a - 1;
    if ((38 + 1) == 39) a = a + 1; else a = a - 1;
    if ((39 + 1) == 40) a = a + 1; else a = a - 1;
    if ((40 + 1) == 41) a = a + 1; else a = a - 1;
    if ((41 + 1) == 42) a = a + 1; else a = a - 1;
    if ((42 + 1) == 43) a = a + 1; else a = a - 1;
    if ((43 + 1) == 44) a = a + 1; else a = a - 1;
    if ((44 + 1) == 45) a = a + 1; else a = a - 1;
    if ((45 + 1) == 46) a = a + 1; else a = a - 1;
    if ((46 + 1) == 47) a = a + 1; else a = a - 1;
    if ((47 + 1) == 48) a = a + 1; else a = a - 1;
    if ((48 + 1) == 49) a = a + 1; else a = a - 1;
    if ((49 + 1) == 50) a = a + 1; else a = a - 1;
    if ((50 + 1) == 51) a = a + 1; else a = a - 1;
    if ((51 + 1) == 52) a = a + 1; else a = a - 1;
    if ((52 + 1) == 53) a = a + 1; else a = a - 1;
    if ((53 + 1) == 54) a = a + 1; else a = a - 1;
    if ((54 + 1) == 55) a = a + 1; else a = a - 1;
    if ((55 + 1) == 56) a = a + 1; else a = a - 1;
    if ((56 + 1) == 57) a = a + 1; else a = a - 1;
    if ((57 + 1) == 58) a = a + 1; else a = a - 1;
    if ((58 + 1) == 59) a = a + 1; else a = a - 1;
    if ((59 + 1) == 60) a = a + 1; else a = a - 1;
    if (a != 60) return 1;
    return 42;
}
