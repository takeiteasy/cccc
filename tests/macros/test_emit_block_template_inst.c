// @template, @inst and Name@(...) work inside an emit block, including a
// template body and an @inst whose name is on the next line.

#pragma cccc comptime begin
#pragma cccc emit begin
@template(T)
T mx(T a, T b) {
    return a > b ? a : b;
}

@template(T)
struct Box {
    T value;
};

int use_inst(void) {
    return @inst(int) mx(1, 42);
}

int use_postfix(void) {
    return mx@(int)(1, 2);
}

int use_wrapped(void) {
    @inst(int)
    Box b = {2};
    return b.value;
}
#pragma cccc emit end
#pragma cccc comptime end

int main(void) {
    return use_inst() + use_postfix() * 0 + use_wrapped() * 0;
}
