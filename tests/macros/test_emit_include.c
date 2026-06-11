// Test #include [[cccc::emit]]: the directive is routed to serialized output
// without entering the runtime translation unit.

#include [[cccc::emit]] <stddef.h>
#include [[cccc::emit]] <stddef.h>

[[cccc::comptime]]
void gen_answer(void) {
    $obj_t *fn = $function("get_answer", $get_type("int"));
    $function_set_body(fn, $return($int_literal(42)));
}

gen_answer();

int get_answer(void);

int main(void) {
    return get_answer();
}
