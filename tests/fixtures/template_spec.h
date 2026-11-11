#pragma once

@ template(T) T twice(T v) {
    return v + v;
}
@specialize(int) int twice(int v) {
    return v * 3;
}
