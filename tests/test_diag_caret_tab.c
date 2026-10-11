// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: \n[ ]+[	][	] +\^ error
//
// The caret line mirrors leading tabs instead of counting them as zero width.

int main(void) {
		__asm__("nop" : "=r"(1));
	return 42;
}
