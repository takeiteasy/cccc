// CCCC_FLAGS: --pointer-sanitizer
// The unsigned loads are the plain forms when a safety check is on: the
// peephole and indexed fusion are off, and each load keeps its check.
#include "test_vm_load_unsigned.c"
