// A switch built with MakeSwitch copies with its cases and default. Cases
// are laid out in order and do not break, so case 2 is last.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *sw = MakeSwitch(MakeIntLiteral(2));
    WithSwitch(sw) {
        SwitchSetDefault(Quote("g += 100;"));
        SwitchAddCase(MakeIntLiteral(1), Quote("g += 100;"));
        SwitchAddCase(MakeIntLiteral(2), Quote("g += 21;"));
    }
    Node *x[2] = {sw, CopyNode(sw)};
    return __builtin_ast_block(x, 2);
}

int main(void) {
    probe();
    return g == 42 ? 42 : 1;
}
