// Fixture for tests/test_empty_tu_999.c (#999).
//
// Deliberately declares nothing but a typedef -- no variable, no function,
// not even a bare prototype -- so parse() creates zero Objs for this TU
// and returns NULL, the same return value it uses for "no new globals
// were created" in general (its own comment says so). Listed first via
// CCCC_FLAGS so main.c's per-TU parse loop reaches it before the real TU.
typedef int EmptyTuMarker;
