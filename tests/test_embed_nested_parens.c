// CCCC_FLAGS: --std=c23
// Test #embed with nested parentheses in parameters
// This tests that the parameter parser correctly handles depth tracking

// Helper macro to simulate nested parentheses
#define NESTED(x, y) ((x) + (y))

int main() {
    // test_data.bin contains 3 bytes
    unsigned char data[] = {
        #embed "embed_data/test_data.bin" prefix(NESTED(1, 2), 5,)
    };

    // NESTED(1, 2) should expand to ((1) + (2)) = 3
    // So we should have: 3, 5, byte0, byte1, byte2
    int size = sizeof(data);
    if (size != 5) {
        return 1;  // Wrong size
    }

    // Verify prefix values
    if (data[0] != 3 || data[1] != 5) {
        return 2;  // Prefix values incorrect
    }

    return 42;  // Success
}
