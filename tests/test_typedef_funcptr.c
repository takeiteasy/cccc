// Test function pointer typedef

typedef int (*callback_t)(int);

int double_it(int x) {
    return x + x;
}

int apply(callback_t func, int value) {
    return func(value);
}

int main() {
    callback_t f = double_it;
    int result = apply(f, 21);  // 21 * 2 = 42
    return result;
}
