// Test sizeof with enums
enum Color { RED, GREEN, BLUE };

int main() {
    enum Color c = RED;
    
    // In CCCC, sizeof(int) = 4, so sizeof(enum) should also be 4
    // since enums are treated as integers
    int s = sizeof(enum Color);
    
    // Verify it's correct
    if (s != 4) return 1;
    
    return 42;  // Success!
}
