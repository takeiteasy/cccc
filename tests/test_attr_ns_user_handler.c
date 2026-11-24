// CCCC_REJECT_STDERR: is not a
// A user handler takes a name the registry only ignores, in any namespace.
@comptime(attribute("cold")) void cold_handler(AttrTarget *t) {
    (void)t;
}
[[cold]] int a;
[[acme::cold]] int b;

int main(void) {
    return 42;
}
