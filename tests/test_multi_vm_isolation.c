// Test that multiple CCCC instances have isolated state
// This verifies the fix for global static variables moved to CCCC struct

int main() {
    // Test 1: __COUNTER__ isolation
    // Each compilation should have its own counter starting from 0
    int c1 = __COUNTER__;  // Should be 0
    int c2 = __COUNTER__;  // Should be 1
    int c3 = __COUNTER__;  // Should be 2

    if (c1 != 0) return 1;
    if (c2 != 1) return 2;
    if (c3 != 2) return 3;

    // Test 2: Anonymous globals (uses unique_name_counter)
    // String literals create anonymous global variables with unique names
    char *s1 = "hello";
    char *s2 = "world";
    char *s3 = "test";

    // They should all be different pointers
    if (s1 == s2) return 4;
    if (s2 == s3) return 5;
    if (s1 == s3) return 6;

    // Success
    return 42;
}
