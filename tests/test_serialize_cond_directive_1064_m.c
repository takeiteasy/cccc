// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main
// CCCC_REJECT_STDOUT: ^[ \t]*#(if|endif|else|elif)
// -m output replays no conditional directive.
#ifdef __CCCC__
#define TOOK_TAKEN_BRANCH 1
#endif
#if 1
#define ALSO_TAKEN 1
#endif
#ifndef TOOK_TAKEN_BRANCH
#error "taken-branch #define was lost"
#endif
#ifndef ALSO_TAKEN
#error "taken #if 1 branch was lost"
#endif
int main(void) {
    return 42;
}
