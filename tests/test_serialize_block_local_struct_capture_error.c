// CCCC_FLAGS: -m
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: declared inside a function
//
// #965: a by-value capture whose own struct/union type is declared inside a
// function (rather than at file scope) can't be represented in a block's
// environment struct -- that struct is emitted at file scope, ahead of the
// function that would otherwise bring the tag into scope. Without this
// check, serialize_type/serialize_anon_aggregate silently inlined an
// anonymous copy of the struct body at each point of use instead, which
// does not match the capture's real, differently-scoped type -- confirmed
// against a real clang "assigning to ... from incompatible type" error
// before this diagnostic was added. Rejected with a diagnostic rather than
// emitting that broken C; tracked as a follow-up ticket, not fixed here.

int main(void) {
    struct P { int x; };
    struct P p = {21};
    int (^b)(void) = ^{ return p.x; };
    return b() + 21;
}
