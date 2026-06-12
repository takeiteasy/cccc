// Test C23 bool/true/false as keywords, without <stdbool.h>

bool global_flag = true;

bool is_even(int n) {
    return n % 2 == 0;
}

int main(void) {
    bool a = true;
    bool b = false;

    if (a != true) return 1;
    if (b != false) return 2;
    if (a == b) return 3;

    // sizeof
    if (sizeof(bool) != 1) return 4;
    if (sizeof(true) != 1) return 5;

    // arithmetic / promotion
    int sum = a + b + true + false;
    if (sum != 2) return 6;

    // comparisons
    if (!(5 > 3) != false) return 7;
    if ((5 > 3) != true) return 8;

    // logical operators
    if ((a && b) != false) return 9;
    if ((a || b) != true) return 10;
    if (!a != false) return 11;
    if (!b != true) return 12;

    // function returning bool
    if (is_even(4) != true) return 13;
    if (is_even(3) != false) return 14;

    // global
    if (global_flag != true) return 15;

    // assignment / normalization: any nonzero value becomes 1
    bool c = 42;
    if (c != true) return 16;
    if ((int)c != 1) return 17;

    return 42;
}
