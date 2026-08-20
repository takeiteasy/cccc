// Regression test for ticket #624: user TK_* macros (e.g. SQLite's
// #define TK_FLOAT 154) must not corrupt reflection.h's TypeKind enum.
// implicit_reflection_tokens() now snapshots and clears the user macro table
// before preprocessing reflection.h, so user TK_* macros cannot interfere.
#define TK_VOID   100
#define TK_BOOL   101
#define TK_CHAR   102
#define TK_INT    103
#define TK_LONG   104
#define TK_FLOAT  154
#define TK_DOUBLE 200
#define TK_STRUCT 300

// A comptime annotation triggers ensure_reflection_attrs_registered(), which
// preprocesses reflection.h.  Compilation succeeding is the regression guard.
[[cccc::comptime]]
Node *identity42(void) {
    return MakeIntLiteral(42);
}

int main(void) {
    return identity42();
}
