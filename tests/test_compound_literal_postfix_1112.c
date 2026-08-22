// #1112: a compound literal is a postfix-expression like any other --
// `.member` / `[index]` / `->member` bind tighter than any unary operator
// above it (C99 6.5.2p5). postfix() used to return directly from its
// compound-literal branch without running the shared tail loop, making all
// of these syntax errors ("expected ','") unless the literal was wrapped in
// parentheses first; real compilers accept the bare form.
//
// Covers reads through every tail kind, plus & of a member through a
// literal -- whose serialized spelling is the #1102 comma-chain restructure
// (`(memset(...), t.x = ..., &t.x)`), so this doubles as a native
// round-trip guard for both tickets.
//
// Expected return: 42

struct Point {
    int x, y;
};

struct Wrap {
    struct Point in;
    int          tag;
};

int main(void) {
    // .member on an aggregate literal.
    int mx = (struct Point){30, 12}.x;
    int my = (struct Point){30, 12}.y;

    // Nested members through a literal.
    struct Wrap w   = {{9, 10}, 77};
    int         mfn = ((struct Wrap){{9, 10}, 77}).in.x;
    int         tag = w.tag;

    // [index] on an array literal.
    int a0 = (int[]){5, 6, 7}[0];
    int a2 = (int[]){5, 6, 7}[2];

    // ->member on a pointer-typed literal ('->' lowers to an explicit
    // deref under the member access).
    struct Point local = {50, 51};
    int          pv    = ((struct Point *){&local})->y;

    // Address-of a member through a literal: & binds inside the literal's
    // initializer chain, covering the whole postfix shell.
    int *pm = &((struct Point){40, 41}).y;

    return mx == 30 && my == 12 && mfn == 9 && tag == 77 && a0 == 5 &&
                   a2 == 7 && pv == 51 && *pm == 41
               ? 42
               : 1;
}