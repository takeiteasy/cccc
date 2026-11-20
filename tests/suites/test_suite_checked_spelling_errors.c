// CCCC_FLAGS: --testing --checked-pointers
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: malformed Checked C spellings are rejected.

[[cccc::test(error = "cannot have a bounds declaration")]]
void single_pointer_with_bounds(void) {
    _Ptr<int> p : count(1) = 0;
}

[[cccc::test(error = "requires the pointer to also be declared")]]
void plain_pointer_with_bounds(void) {
    int *p : count(1) = 0;
}

[[cccc::test(error = "expected '>' to close '_Ptr<'")]]
void unterminated_angle(void) {
    _Ptr < int p;
}

[[cccc::test(error = "'bounds' requires two arguments")]]
void bounds_needs_two_arguments(void) {
    _Array_ptr<int> p : bounds(1) = 0;
}
