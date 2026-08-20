// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: argument may be null when passed to a parameter marked
// nonnull \(parameter 1\) #688's own motivating example verbatim: the null
// literal is nested inside a ternary used as the return operand itself, not a
// separate `return 0;` statement. This exercises nn_expr_may_be_null()'s
// handling of ND_COND as a value-producing expression (distinct from
// nn_walk_branch / nn_returns_null_walk's statement-level if/else dead-branch
// pruning).
int cond = 1;
int x    = 0;
int *maybe_null(void) {
    return cond ? &x : 0;
}

void handle(int *p) __attribute__((nonnull));
void handle(int *p) {
    *p = 1;
}

void use(void) {
    int *p = maybe_null();
    handle(p);
}

int main(void) {
    use();
    return 42;
}
