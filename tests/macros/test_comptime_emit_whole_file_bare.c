// Test bare #pragma cccc emit (no begin/end) inside a bare #pragma cccc comptime block.
// Both are whole-file (bare) forms; both close silently at EOF.

#pragma cccc comptime

int comptime_helper(void) { return 1; }

#pragma cccc emit

int runtime_helper(void) { return 42; }

int main(void) {
    return runtime_helper();
}
