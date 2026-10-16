// CCCC_REJECT_STDERR: unknown attribute
#pragma cccc diagnostic push
#pragma cccc diagnostic ignored "-Wunknown-attributes"
int x @no_such_handler;
#pragma cccc diagnostic pop

int main(void) {
    return 42;
}
