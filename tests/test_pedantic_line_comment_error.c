/* EXPECT_COMPILE_ERROR */
/* CCCC_FLAGS: --std=c89 -Werror=pedantic */
/* CCCC_EXPECT_STDERR: error: '//' comments are a C99 extension \[-Wpedantic\]
 */
int main(void) {
    return 42; // promoted tokenizer pedantic warning
}
