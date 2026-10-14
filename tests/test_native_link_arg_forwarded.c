// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=native --link-arg=-lcccc_no_such_lib -o build/test_native_link_arg_forwarded
// CCCC_EXPECT_STDERR: cccc_no_such_lib
//
// --link-arg reaches the host linker verbatim: a library that does not exist
// makes the link fail naming it.

int main(void) {
    return 42;
}
