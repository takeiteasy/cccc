// CCCC_FLAGS: tests/fixtures/multi_tu_unique_static_1002_a.c -m
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_EXPECT_STDOUT: static int test_1002_helper\(void\)
//
// Found investigating #1002 (not what that ticket itself reported, but it
// blocks the ticket's own repro from being observable -- see CLAUDE.md):
// function_is_header_supplied() (src/serialize.c) used to identify "was
// this static function supplied by a replayed #include" by comparing
// against vm->compiler.primary_file alone, which cc_preprocess/linker.c pin
// to input_files[0] forever. A static function with a body, defined in any
// *non-first* command-line input file -- this file is TU2, the fixture
// above (listed first via CCCC_FLAGS) is TU1/primary -- was therefore
// misidentified as header-supplied and silently dropped from -m/-c=native
// output, with nothing else in the output supplying it: a native build of
// this exact shape failed with "call to undeclared function". Fixed via
// file_is_command_line_input(), keyed off every command-line input path,
// not just the first.
static int test_1002_helper(void) {
    return 22;
}

int multi_tu_1002_fixture_entry(void);

int main(void) {
    return multi_tu_1002_fixture_entry() + test_1002_helper();
}
