// Tests: <% and %> digraphs as { and } block delimiters (C23 §6.4.6)
#include <stdio.h>

int square(int x) <%
    return x * x;
%>

int main(void) <%
    int r = square(7);
    return r == 49 ? 42 : 1;
%>
