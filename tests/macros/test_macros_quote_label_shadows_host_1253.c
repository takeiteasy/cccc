// A Quote() template that both defines a label and jumps to it binds to its
// own label, not to a same-named label in the enclosing function. The
// hygienic in-template resolution pass matches the template's goto against
// the template's own `done:` and unlinks both before the host's own
// resolve_goto_labels runs.

[[cccc::comptime]]
Node *g(Node *unused) {
    return Quote("{ goto done; return 1; done: return 42; }");
}

int test(void) {
    volatile int never = 0;
    g(0); // splices a block whose `done:` returns 42
    if (never)
        goto done; // keep the host `done:` referenced; never taken
    return 7;
done:
    return 99; // the host `done:` -- the template must not reach here
}

int main(void) {
    return test();
}
