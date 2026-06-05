// Ticket #233: ND_BLOCK returned from a file-scope macro call is unwrapped and
// its declarations are spliced directly into global scope.

[[jcc::macro]]
_Node *emit_widget_helpers(void) {
    return _QUOTE("{ struct Widget { int x; int y; }; void widget_init(struct Widget *w) { w->x = 0; w->y = 0; } void widget_set(struct Widget *w, int x, int y) { w->x = x; w->y = y; } }");
}

emit_widget_helpers();

int main(void) {
    struct Widget w;
    widget_init(&w);
    if (w.x != 0 || w.y != 0)
        return 1;
    widget_set(&w, 3, 7);
    if (w.x != 3 || w.y != 7)
        return 2;
    return 42;
}
