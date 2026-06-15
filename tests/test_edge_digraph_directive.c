// Tests: %: digraph as # in preprocessor directive lines (C23 §6.4.6)
%:include <stdio.h>

%:define ANSWER 42

int main(void) <%
    return ANSWER == 42 ? 42 : 1;
%>
