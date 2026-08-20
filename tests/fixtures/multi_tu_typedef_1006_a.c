// Fixture for tests/test_serialize_multi_tu_typedef.c (#1006).
//
// TU1 (this file, input_files[0] / "the primary file"). Deliberately empty
// of anything the test's own assertions care about -- the whole point is
// that TU2 (the test file itself) is the *non-primary* translation unit,
// which is where #1006's drops (a file-scope typedef treated as
// header-supplied; a non-primary TU's own #include never replayed) bit.
int multi_tu_typedef_1006_a(void) {
    return 1;
}
