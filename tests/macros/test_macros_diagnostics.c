// Test ticket #78: __builtin_macro_warning_at emits a source-located warning.
// The warning should reference the call-site node's source location.
// We verify compilation completes (warning is non-fatal) and the
// program runs to the correct exit code.

// Macro that emits a warning at the argument's source location,
// then returns the argument unchanged.
[[cccc::comptime]]
Node *warn_if_zero(Node *n) {
    // Always emit a warning — we're testing the mechanism, not the logic
    __builtin_macro_warning_at(n, "warn_if_zero: inspecting argument (test warning)");
    return n;
}

int main(void) {
    // The warning is emitted during compilation; the program still runs.
    int x = warn_if_zero(99);
    if (x != 99) return 1;
    return 42;
}
