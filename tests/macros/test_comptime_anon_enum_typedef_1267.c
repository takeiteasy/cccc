// #1267: a header defining an *anonymous* `typedef enum { ... } EKind;` that
// is reached BOTH by `#include @comptime` in this primary input AND (its
// helper's body, on demand) by a second command-line .c that #includes it the
// ordinary way (#1243). cccc used to report a spurious "redeclaration of
// enumerator 'V_A'" and emit nothing: declspec's continuation probe on the
// `EKind` declarator name fired the #894 demand-driven splice, re-parsing the
// second file's identical copy of the statement. A tagged enum deduped fine;
// only the anonymous form -- which has no tag identity for
// enum_specifier's duplicate suppression to key on -- tripped.
//
// CCCC_FLAGS: tests/fixtures/comptime_anon_enum_1267_mod.c
#include @comptime "tests/fixtures/comptime_anon_enum_1267.h"

[[cccc::comptime]]
void gen(void) {
    // Use an enumerator from the anonymous typedef and call the forwarded
    // helper, so both the enum-dedup path and #1243 body forwarding run.
    Obj *fn = MakeFunction("answer", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(helper(41) + V_B)));
}
gen();

int main(void) {
    return answer(); // helper(41) == 41 + V_A(0), V_B == 1 -> 42
}
