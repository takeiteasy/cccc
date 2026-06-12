// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: custom attributes are only supported on file-scope declarations

@macro(attribute("local_only"))
void local_only_attr($attr_target_t *target) {
    (void)target;
}

int main(void) {
    @local_only int x = 1;
    return x;
}
