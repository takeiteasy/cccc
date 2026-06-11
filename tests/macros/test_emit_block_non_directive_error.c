// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: only preprocessor directives are allowed inside #pragma cccc emit blocks

#pragma cccc emit begin
int not_a_directive;
#pragma cccc emit end

int main(void) {
    return 42;
}
