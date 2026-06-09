// Test __cccc_forward_include: called from a macro body to register headers
// that should be prepended to serialized output. Duplicate calls for the
// same header must be collapsed to a single #include in the output.

[[cccc::comptime]]
void gen_answer(void) {
    $vm_t *vm = _VM;
    // Register the same header twice — output must contain only one entry.
    __cccc_forward_include(vm, "<stddef.h>");
    __cccc_forward_include(vm, "<stddef.h>");
    __cccc_forward_include(vm, "<stdint.h>");
    $obj_t *fn = $function("get_answer", $get_type("int"));
    $function_set_body(fn, $return($int_literal(42)));
}

gen_answer();

int get_answer(void);

int main(void) {
    return get_answer();
}
