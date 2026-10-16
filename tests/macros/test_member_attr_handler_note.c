// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: member handler rejected this
// CCCC_EXPECT_STDERR: note: while handling attribute '@tag' on 'member x of struct P'

@comptime(attribute("tag")) void tag_member(AttrTarget *target) {
    MacroErrorAt(MakeIntLiteral(0), "member handler rejected this");
}

struct P {
    int x @tag;
};

int main(void) {
    return 42;
}
