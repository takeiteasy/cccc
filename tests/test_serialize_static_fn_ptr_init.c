// CCCC_FLAGS: -m
// CCCC_C4_SKIP: -m dumps source and exits, no bytecode to round-trip
// CCCC_EXPECT_STDOUT: static int none_open\(void\);[\s\S]*static const VT kNoneVT = \{ \.open = \(int \(\*\)\(void\)\)\(\(char \*\)&none_open
//
// #999: dandy's collector-vtable pattern -- a `static const` struct of
// function pointers initialized from file-static functions declared (but
// not yet defined) earlier in the file. cc_serialize_program's global-
// definitions pass ran before the function-prototype pass, so a global
// initializer that takes a later static function's address by name
// reached the output with nothing declaring that name yet ("use of
// undeclared identifier"). Verified the prototype for none_open appeared
// *after* kNoneVT's own initializer in -m output before the fix.
typedef struct {
    int (*open)(void);
    int (*close)(void);
} VT;

static int none_open(void);
static int none_close(void);

static const VT kNoneVT = { .open = none_open, .close = none_close };

static int none_open(void) { return 20; }
static int none_close(void) { return 22; }

int main(void) { return kNoneVT.open() + kNoneVT.close(); }
