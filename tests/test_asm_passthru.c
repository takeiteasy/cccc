// Test inline assembly passthru with --asm-passthru flag
// JCC_FLAGS: --asm-passthru
// Expected return: 42

int main() {
    int result = 10;

    // Test 1: Basic asm statement (compiled and executed via FFI)
    asm("nop");

    // Test 2: asm volatile
    asm volatile("nop");

    // Test 3: Multiple asm statements
    asm("nop");
    asm("nop");

    // Normal code continues after asm execution
    result = 42;

    return result;
}
