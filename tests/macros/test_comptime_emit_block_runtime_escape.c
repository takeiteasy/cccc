#pragma cccc comptime begin
int comptime_helper(void) { return 1; }

#pragma cccc emit begin
int runtime_helper(void) { return 42; }
#pragma cccc emit end
#pragma cccc comptime end

int main(void) {
    return runtime_helper();
}
