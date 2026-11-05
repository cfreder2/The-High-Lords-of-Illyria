#pragma once
// The integer square root (the largest x with x * x <= n): exact on every machine, for the places in a step that
// would otherwise reach for the platform's floating-point square root.
#include <cstdint>

namespace illyria::sim {

inline int64_t isqrt(int64_t n) {
    if (n <= 0) return 0;
    int64_t x = n, y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

}  // namespace illyria::sim
