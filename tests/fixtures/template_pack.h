#pragma once

@ template(Ts...) int pack_sum(Ts... xs) {
    return (xs + ... + 0);
}
@ template(Ts...) struct PackTuple {
    Ts... v;
};
