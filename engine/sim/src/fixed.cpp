#include "illyria/sim/fixed.h"

#include <array>

namespace illyria::sim {

namespace {

// Integer square root: floor(sqrt(n)), exact (so any method gives the same bits). Squared distances are almost always
// under 2^64: Newton's method in 64-bit arithmetic from an estimate that is never below the root and within 0.2% of
// it (from the top byte of n, scaled up to [2^62, 2^64)), so that two divisions leave it within one of the root.
// kRootAbove[i] is the ceiling of the root of the top of the bucket (i + 64 + 1) << 56, as the root of such a n is
// in [2^31, 2^32].
constexpr std::array<uint64_t, 192> kRootAbove = [] {
    std::array<uint64_t, 192> t{};
    for (size_t i = 0; i < t.size(); ++i) {
        const u128 top = static_cast<u128>(i + 64 + 1) << 56;
        u128 lo = 0, hi = u128{1} << 33;  // the least r with r * r >= top
        while (lo < hi) {
            const u128 mid = (lo + hi) / 2;
            if (mid * mid >= top) hi = mid;
            else lo = mid + 1;
        }
        t[i] = static_cast<uint64_t>(lo);
    }
    return t;
}();

uint64_t isqrt64(uint64_t n) {
    if (n < 2) return n;
    const unsigned up = static_cast<unsigned>(__builtin_clzll(n)) & ~1u;  // n << up is in [2^62, 2^64)
    uint64_t x = kRootAbove[((n << up) >> 56) - 64] >> (up / 2);          // >= the root
    for (;;) {
        const uint64_t y = (x + n / x) / 2;
        if (y >= x) return x;
        x = y;
    }
}

// The rest (n up to 2^79): the bit-by-bit method.
uint64_t isqrt(u128 n) {
    if ((n >> 64) == 0) return isqrt64(static_cast<uint64_t>(n));
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
