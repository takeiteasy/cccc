// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int carr\[3\];.*= 1 , \(\*\(\(int \*\)
// CCCC_REJECT_STDOUT: const int carr
//
// #1102 (the const-qualified companion of #1029's hoisting rule): a
// const-element aggregate local (`const int carr[3] = {1,2,3}`) has its
// declaration hoisted and its initializer split into per-element
// assignments -- but C spells the qualifier on the *element* type, one
// level further down than #1029's top-level strip looked, so the generated
// C kept a genuinely-const object and stored into it through the usual
// byte-offset cast chain. Every host compiler rejects that statically
// ("read-only variable is not assignable"), even though cccc's own AST
// treats initializer stores as pre-const initialization. The hoisted
// declarator now drops the element qualifier (and the cast-back spelling
// drops pointee qualifiers to match), so the object is mutable exactly as
// long as it is being initialized and only read afterwards. The REJECT
// pins the old `const int carr[3];` declaration away.

int main(void) {
    const int carr[3] = {1, 2, 3};
    return carr[0] + carr[2] == 4 ? 42 : 1;
}
