// CCCC_EXPECT_STDOUT: file.*local.*multi
// Ticket #438: asm labels on typedef function declarations, including
// multi-declarator block-scope declarations.

typedef int Put(const char *);

Put file_puts asm("puts");

int main(void) {
    Put local_puts asm("puts"), local_puts2 asm("puts");

    file_puts("file");
    local_puts("local");
    local_puts2("multi");
}
