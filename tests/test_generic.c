// Test _Generic type-generic expressions
// Tests compile-time type selection based on controlling expression type

// Helper functions to identify which type was selected
int int_func(int x) { return 1; }
int long_func(long x) { return 2; }
int float_func(float x) { return 3; }
int double_func(double x) { return 4; }
int ptr_func(int *x) { return 5; }
int char_func(char x) { return 6; }
int default_func(void *x) { return 99; }

int main() {
    // Test 1: Basic _Generic with int
    int i = 10;
    int result = _Generic(i,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func
    )(i);
    if (result != 1) return 1;
    
    // Test 2: _Generic with long
    long l = 20;
    result = _Generic(l,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func
    )(l);
    if (result != 2) return 2;
    
    // Test 3: _Generic with double
    double d = 3.14;
    result = _Generic(d,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func
    )(d);
    if (result != 4) return 3;
    
    // Test 4: _Generic with pointer
    int *p = &i;
    result = _Generic(p,
        int: int_func,
        int*: ptr_func,
        double: double_func,
        default: default_func
    )(p);
    if (result != 5) return 4;
    
    // Test 5: _Generic with char
    char c = 'A';
    result = _Generic(c,
        int: int_func,
        char: char_func,
        double: double_func,
        default: default_func
    )(c);
    if (result != 6) return 5;
    
    // Test 6: _Generic selecting values directly (not functions)
    int value = _Generic(i,
        int: 100,
        long: 200,
        double: 300,
        default: 999
    );
    if (value != 100) return 6;
    
    // Test 7: _Generic with default case
    short s = 5;
    result = _Generic(s,
        int: int_func,
        long: long_func,
        double: double_func,
        default: default_func
    )(&s);
    if (result != 99) return 7;
    
    // Test 8: _Generic with expression as controlling expression
    result = _Generic(i + l,
        int: 10,
        long: 20,
        double: 30,
        default: 40
    );
    if (result != 20) return 8;  // int + long = long
    
    // Test 9: _Generic with const type
    const int ci = 50;
    value = _Generic(ci,
        int: 111,
        const int: 222,
        default: 333
    );
    if (value != 111) return 9;
    
    // Test 10: Multiple _Generic in same expression
    int sum = _Generic(i, int: 5, default: 0) + 
              _Generic(d, double: 10, default: 0);
    if (sum != 15) return 10;
    
    return 42;  // Success
}
