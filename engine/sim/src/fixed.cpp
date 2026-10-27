#include "illyria/sim/fixed.h"

namespace illyria::sim {

namespace {

// Bit-by-bit integer square root: floor(sqrt(n)).
uint64_t isqrt(u128 n) {
    u128 result = 0;
    u128 bit = u128{1} << 126;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= result + bit) {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<uint64_t>(result);
}

// Taylor series for sin(x) on [-pi/2, pi/2] evaluated in Horner form:
// x * (1 - x^2/6 * (1 - x^2/20 * (1 - x^2/42 * (1 - x^2/72 * (1 - x^2/110)))))
// Truncation error at pi/2 is ~6e-8, well below Fixed resolution.
Fixed sinReduced(Fixed x) {
    const Fixed x2 = x * x;
    Fixed t = Fixed::one() - x2 / 110;
    t = Fixed::one() - x2 * t / 72;
    t = Fixed::one() - x2 * t / 42;
    t = Fixed::one() - x2 * t / 20;
    t = Fixed::one() - x2 * t / 6;
    return x * t;
}

}  // namespace

Fixed sqrt(Fixed f) {
    if (f.raw <= 0) return Fixed::zero();
    // sqrt(raw / 2^16) * 2^16 == sqrt(raw * 2^16)
    return Fixed::fromRaw(static_cast<int64_t>(isqrt(static_cast<u128>(f.raw) << Fixed::kFracBits)));
}

Fixed sin(Fixed x) {
    // Reduce to [-pi, pi).
    x.raw %= kTwoPi.raw;
    if (x >= kPi) x -= kTwoPi;
    if (x < -kPi) x += kTwoPi;
    // Fold into [-pi/2, pi/2] using sin(pi - x) == sin(x).
    if (x > kHalfPi) x = kPi - x;
    if (x < -kHalfPi) x = -kPi - x;
    return sinReduced(x);
}

Fixed cos(Fixed x) { return sin(x + kHalfPi); }

}  // namespace illyria::sim
