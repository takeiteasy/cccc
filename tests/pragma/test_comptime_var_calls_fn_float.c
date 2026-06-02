// Ticket #191: comptime float variable initializer calls a comptime function.

#pragma comptime
double get_scale_factor(void) { return 1.5; }

#pragma comptime
double area = get_scale_factor() * 200.0;

#pragma macro
_Node *get_area_int(void) {
    // Cast to int for exact comparison: 1.5 * 200.0 == 300.
    return _AST_INT_LITERAL((int)_AST_GET_COMPTIME_FLOAT("area"));
}

int main(void) {
    if (get_area_int() != 300)
        return 1;
    return 42;
}
