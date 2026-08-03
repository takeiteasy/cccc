// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pointer/string variables are not supported yet
// Ticket #886 fixed typedefs inside comptime-executed code (they are type
// declarations, not comptime variables). This must not widen ticket #188's
// deliberate restriction: a genuine pointer/string *comptime variable* --
// as opposed to a type declaration -- is still rejected.

#pragma cccc comptime begin
char *s = "hello";
#pragma cccc comptime end

int main(void) { return 42; }
