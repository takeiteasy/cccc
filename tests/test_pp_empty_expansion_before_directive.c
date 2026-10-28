// An empty macro expansion must not clear the line-start state of the
// following token: the next line's directive is still a directive.
#define FAM(X) X(a) X(b)
#define NONE(X)

static const char *const names[] = {
#define ROW(n) #n,
    FAM(ROW) NONE(ROW)
#undef ROW
};

#define EMPTY
EMPTY
#define AFTER_EMPTY 40

int main(void) {
    return sizeof names / sizeof *names + AFTER_EMPTY;
}
