#pragma cccc comptime begin
#pragma cccc emit begin
@define CT_FROM_EMIT 99
#pragma cccc emit end
#pragma cccc comptime end

[[cccc::comptime]]
Node *get_val(void) { return MakeIntLiteral(CT_FROM_EMIT); }

int main(void) { return get_val() - 57; }
