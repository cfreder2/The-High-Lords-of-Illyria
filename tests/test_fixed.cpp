#include <cmath>

#include "illyria/sim/fixed.h"
#include "test.h"

using namespace illyria::sim;

TEST(fixed_arithmetic) {
    const Fixed a = Fixed::fromRatio(3, 2);
    const Fixed b = Fixed::fromInt(-4);
    CHECK((a + b) == Fixed::fromRatio(-5, 2));
    CHECK((a - b) == Fixed::fromRatio(11, 2));
    CHECK((a * b) == Fixed::fromInt(-6));
    CHECK((b / a) == Fixed::fromRatio(-8, 3));
    CHECK((a * 4) == Fixed::fromInt(6));
    CHECK(Fixed::fromRatio(-7, 2).floorToInt() == -4);
}

TEST(fixed_large_products_do_not_overflow) {
    // Squared distance across a 20 km map, squared again.
    const Fixed d = Fixed::fromInt(20000);
    const Fixed d2 = d * d;
    CHECK(d2 == Fixed::fromInt(400000000));
    CHECK((d2 / d) == d);
}

TEST(fixed_sqrt) {
    CHECK(sqrt(Fixed::fromInt(144)) == Fixed::fromInt(12));
    CHECK(sqrt(Fixed::zero()) == Fixed::zero());
    CHECK(sqrt(Fixed::fromInt(-1)) == Fixed::zero());
    CHECK_NEAR(sqrt(Fixed::fromInt(2)).toDouble(), std::sqrt(2.0), 2e-5);
    const Fixed small = Fixed::fromRatio(1, 100);
    CHECK_NEAR(sqrt(small).toDouble(), std::sqrt(small.toDouble()), 2e-5);
    CHECK_NEAR(sqrt(Fixed::fromInt(1000000000)).toDouble(), std::sqrt(1e9), 2e-5);
}

// sqrt is the floor of the exact root of raw * 2^16, whichever way it is computed (the quick 64-bit path or the long one).
TEST(fixed_sqrt_is_the_exact_floor_root) {
    uint64_t s = 88172645463325252ULL;  // xorshift, a fixed stream
    auto next = [&] { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    for (int i = 0; i < 200000; ++i) {
        const int64_t raw = static_cast<int64_t>(next() >> (1 + next() % 62));
        if (raw <= 0) continue;
        const u128 n = static_cast<u128>(raw) << Fixed::kFracBits;
        const u128 r = static_cast<u128>(sqrt(Fixed::fromRaw(raw)).raw);
        CHECK(r * r <= n);
        CHECK((r + 1) * (r + 1) > n);
    }
    for (int64_t raw : {int64_t{1}, int64_t{2}, int64_t{3}, int64_t{4}, (int64_t{1} << 48) - 1, int64_t{1} << 48, (int64_t{1} << 48) + 1, INT64_MAX}) {
        const u128 n = static_cast<u128>(raw) << Fixed::kFracBits;
        const u128 r = static_cast<u128>(sqrt(Fixed::fromRaw(raw)).raw);
        CHECK(r * r <= n);
        CHECK((r + 1) * (r + 1) > n);
    }
}

// The quotient is the 128-bit one, truncated toward zero, on both sides of the 64-bit shortcut (|raw| < 2^47).
TEST(fixed_division_is_the_128_bit_quotient) {
    uint64_t s = 1234567890123456789ULL;
    auto next = [&] { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    for (int i = 0; i < 200000; ++i) {
        const int64_t a = static_cast<int64_t>(next()) >> (1 + next() % 62), b = static_cast<int64_t>(next()) >> (1 + next() % 62);
        if (b == 0) continue;
        CHECK((Fixed::fromRaw(a) / Fixed::fromRaw(b)).raw == static_cast<int64_t>((static_cast<i128>(a) << Fixed::kFracBits) / b));
    }
    const int64_t edge = int64_t{1} << 47;
    for (int64_t a : {edge - 1, edge, edge + 1, -edge + 1, -edge, -edge - 1, int64_t{0}, int64_t{-1}}) {
        for (int64_t b : {int64_t{1}, int64_t{-1}, int64_t{3}, int64_t{-65536}, edge}) {
            CHECK((Fixed::fromRaw(a) / Fixed::fromRaw(b)).raw == static_cast<int64_t>((static_cast<i128>(a) << Fixed::kFracBits) / b));
        }
    }
}

TEST(fixed_trig_matches_libm) {
    // libm is only the reference here; the simulation never calls it.
    for (int i = -2000; i <= 2000; i += 7) {
        const Fixed x = Fixed::fromRatio(i, 100);
        CHECK_NEAR(sin(x).toDouble(), std::sin(x.toDouble()), 1e-4);
        CHECK_NEAR(cos(x).toDouble(), std::cos(x.toDouble()), 1e-4);
    }
}
