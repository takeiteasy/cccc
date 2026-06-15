// Tests: <: and :> digraphs as [ and ] in array declarations and subscripts (C23 §6.4.6)
#include <stdio.h>

int main(void) {
    int a<:3:> = {10, 20, 30};
    int sum = a<:0:> + a<:1:> + a<:2:>;
    return sum == 60 ? 42 : 1;
}
