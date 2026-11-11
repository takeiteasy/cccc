@template(T) T max(T a, T b) { return a < b ? a : b; }

int tu_b_value(void) {
    return @inst(int) max(3, 4);
}
