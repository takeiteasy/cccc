// Fixture TU1 for tests/test_serialize_non_primary_static_not_dropped.c
// (commit 1 of the #1001/#1002/#1003 investigation). Deliberately unrelated
// to the second TU's own static -- this file exists only to make the test
// a genuine multi-file compile with a distinct primary_file, matching the
// shape function_is_header_supplied() used to get wrong for input files
// after the first.
int multi_tu_1002_fixture_entry(void) {
    return 20;
}
