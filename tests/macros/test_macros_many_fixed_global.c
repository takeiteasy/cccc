// Ticket #287: global-generation macros can receive fixed string parameters
// beyond the first 8.

[[cccc::comptime]]
void gen_ninth(char *a0, char *a1, char *a2, char *a3, char *a4, char *a5,
               char *a6, char *a7, char *a8) {
    $obj_t *fn = $function(a8, $get_type("int"));
    $function_set_body(fn, $return($int_literal(42)));
}

gen_ninth(skip0, skip1, skip2, skip3, skip4, skip5, skip6, skip7, answer);

int main(void) {
    return answer();
}
