// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: note: while handling attribute '@tag' on 'struct P' at .*test_at_attr_note_location\.c:10:4

@comptime(attribute("tag")) void tag_type(AttrTarget *target) {
    MacroErrorAt(MakeIntLiteral(0), "type handler rejected this");
}

struct P {
    int x;
} @tag;

int main(void) {
    return 42;
}
