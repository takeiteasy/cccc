// Comprehensive enum demonstration for CCCC
// Shows all supported enum features

// Basic enum with implicit values
enum Color {
    RED,      // 0
    GREEN,    // 1
    BLUE      // 2
};

// Enum with explicit values
enum Status {
    IDLE = 0,
    ACTIVE = 10,
    WAITING = 20,
    COMPLETE = 30
};

// Mixed auto/explicit values
enum Priority {
    LOW,           // 0
    MEDIUM = 5,    // 5
    HIGH,          // 6
    CRITICAL = 10  // 10
};

// Bitwise flags
enum Flags {
    FLAG_READ  = 1,   // 0b0001
    FLAG_WRITE = 2,   // 0b0010
    FLAG_EXEC  = 4    // 0b0100
};

// Function using enum parameter
int calculate_score(enum Priority p, enum Status s) {
    return p + s;
}

// Function returning enum
enum Color get_color(int value) {
    if (value < 1) return RED;
    if (value < 2) return GREEN;
    return BLUE;
}

int main() {
    // Test 1: Basic enum usage
    enum Color c = RED;
    if (c != 0) return 1;
    
    c = BLUE;
    if (c != 2) return 2;
    
    // Test 2: Explicit values
    enum Status s = ACTIVE;
    if (s != 10) return 3;
    
    // Test 3: Arithmetic with enums
    int sum = ACTIVE + WAITING;  // 10 + 20 = 30
    if (sum != 30) return 4;
    
    // Test 4: Enum comparisons
    enum Priority p = HIGH;
    if (p != 6) return 5;  // HIGH should be 6
    if (!(p > MEDIUM)) return 5;  // 6 > 5
    
    // Test 5: Bitwise operations with enums
    int perms = FLAG_READ | FLAG_WRITE;  // 0b0011 = 3
    if (perms != 3) return 6;
    
    if ((perms & FLAG_READ) == 0) return 7;   // Check has read
    if ((perms & FLAG_EXEC) != 0) return 8;   // Check no exec
    
    // Test 6: Enum in array subscript
    int values[3] = {100, 200, 300};
    int val = values[GREEN];  // values[1] = 200
    if (val != 200) return 9;
    
    // Test 7: Enum as function parameter
    int score = calculate_score(HIGH, ACTIVE);  // 6 + 10 = 16
    if (score != 16) return 10;
    
    // Test 8: Enum from function return
    enum Color result_color = get_color(2);
    if (result_color != BLUE) return 11;
    
    // Test 9: Comparison operations
    if (HIGH <= MEDIUM) return 12;
    if (!(CRITICAL > HIGH)) return 13;
    
    // Test 10: Using enum in expressions
    int total = LOW + MEDIUM + HIGH + CRITICAL;  // 0 + 5 + 6 + 10 = 21
    if (total != 21) return 14;
    
    // All tests passed!
    return 42;
}
