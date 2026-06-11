// Test alternate include qualifier spellings for emit routing.

#include @emit <stddef.h>
#include __attribute__((emit)) <stdint.h>

[[cccc::comptime]]
void gen_answer(void) {
    $obj_t *fn = $function("emit_spelling_answer", $get_type("int"));
    $function_set_body(fn, $return($int_literal(42)));
}

gen_answer();

int emit_spelling_answer(void);

int main(void) {
    return emit_spelling_answer();
}
