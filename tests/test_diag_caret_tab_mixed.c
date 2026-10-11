// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: \n[ ]+[	] +\^ error
//
// Tab followed by spaces: tab stays a tab, other characters become spaces.

int main(void) {
	  __asm__("nop" : "=r"(1));
	return 42;
}
