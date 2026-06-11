// Test multiple emit begin/end sub-blocks within a single comptime begin/end scope.

#pragma cccc comptime begin

int comptime_val(void) { return 1; }

#pragma cccc emit begin
int runtime_a(void) { return 21; }
#pragma cccc emit end

int comptime_val2(void) { return 2; }

#pragma cccc emit begin
int runtime_b(void) { return 21; }
#pragma cccc emit end

#pragma cccc comptime end

int main(void) {
    return runtime_a() + runtime_b();
}
