// A template defined in a comptime block belongs to the comptime program. A
// runtime template of the same name is separate, and both can make the same
// instance.
#pragma cccc comptime begin
@template(T) T twice(T v) {
    return v * 2;
}

@template(T) struct Box {
    T value;
};

int ct_answer(void) {
    @inst(int) Box b = {10};
    return @inst(int) twice(b.value) + twice @(int)(1);
}

int ct_via_name(void) {
    return twice__int(4);
}
#pragma cccc comptime end

@template(T) T twice(T v) {
    return v + v + v;
}

@comptime void gen(void) {
    Obj *fn = MakeFunction("answer", GetType("int"));
    FunctionSetBody(fn,
                    MakeReturn(MakeIntLiteral(ct_answer() + ct_via_name())));
}
gen();

int answer(void);

int main(void) {
    return answer() + @inst(int) twice(2) + 6; // 30 + 6 + 6
}
