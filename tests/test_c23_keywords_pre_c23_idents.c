// CCCC_FLAGS: --std=c17
// Pre-C23: bool/true/false/nullptr are downgraded to plain identifiers and
// remain usable as variable/function names when <stdbool.h> isn't included.
int bool = 1;
int true = 2;
int false = 3;
int nullptr = 4;

int main(void) {
    int sum = bool + true + false + nullptr;
    if (sum != 10) return 1;
    return 42;
}
