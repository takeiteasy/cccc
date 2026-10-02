// Ticket #1255: a function label whose only goto is spliced in from a Quote()
// template was reported by -Wunused as "unused label", because the warning
// fired at parse time, before macro expansion bound the template's free
// `goto done;` to it. The warning is now deferred until after that binding;
// a genuinely unused label in the same function is still diagnosed.
// CCCC_FLAGS: -Wunused
// CCCC_EXPECT_STDERR: unused label 'stray'
// CCCC_REJECT_STDERR: unused label 'done'

[[cccc::comptime]]
Node *jump_done(Node *unused) {
    return Quote("goto done;");
}

int test(void) {
    jump_done(0);
    return 1;
done:
    return 42;
}

int test2(void) {
    jump_done(0);
    return 1;
stray:
done:
    return 42;
}

int main(void) {
    return test() == 42 && test2() == 42 ? 42 : 1;
}
