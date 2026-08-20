// CCCC_EXPECT_STDOUT: hello world
// Tests: empty attribute [[]] as both typedef name and label; digraph %: as #;
// computed goto via &&label; int typedef [[]]\$ uses \$ as an identifier.
#include <stdio.h>

int typedef[[]] $;

int main($[[]] $) {
[[]] $:
    &&$ && $ && puts("hello world");
}
