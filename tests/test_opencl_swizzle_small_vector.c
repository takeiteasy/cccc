// CCCC_FLAGS: -x cl
// A swizzle that names two lanes of a float4 is an 8-byte float2.
float2 pair(float4 v) {
    return v.xy;
}
int main(void) {
    float4 v = (float4)(1, 2, 3, 4);
    float2 a = pair(v), b = v.hi;
    float4 w = (float4)(v.xy, v.zw);
    return a.x == 1 && a.y == 2 && b.x == 3 && b.y == 4 && w.w == 4 ? 42 : 1;
}
