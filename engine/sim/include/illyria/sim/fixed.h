#pragma once

#include <compare>
#include <cstdint>

namespace illyria::sim {

__extension__ typedef __int128 i128;
__extension__ typedef unsigned __int128 u128;

// Signed fixed-point number: 64-bit storage, 16 fractional bits (Q47.16).
//
// All gameplay math uses this type instead of float/double so that every peer
// in a lockstep match computes bit-identical results regardless of CPU,
// compiler or platform (native x86-64, ARM, or WebAssembly).
//
// Resolution is 1/65536 (~0.015 mm when 1.0 == 1 metre). Products are computed
// in 128 bits, so intermediate values like squared distances never overflow
// on any realistic battlefield.
struct Fixed {
    static constexpr int kFracBits = 16;
    static constexpr int64_t kOneRaw = int64_t{1} << kFracBits;

    int64_t raw = 0;

    static constexpr Fixed fromRaw(int64_t r) { return Fixed{r}; }
    static constexpr Fixed fromInt(int64_t i) { return Fixed{i * kOneRaw}; }
    // num/den, truncated toward zero. Use this for tuning constants: never
    // build a Fixed from a float at runtime.
    static constexpr Fixed fromRatio(int64_t num, int64_t den) {
        return Fixed{static_cast<int64_t>((static_cast<i128>(num) << kFracBits) / den)};
    }

    static constexpr Fixed zero() { return Fixed{0}; }
    static constexpr Fixed one() { return Fixed{kOneRaw}; }

    // Rounds toward negative infinity.
    constexpr int64_t floorToInt() const { return raw >> kFracBits; }
    // For rendering and debugging only; never feed the result back into the simulation.
    constexpr double toDouble() const { return static_cast<double>(raw) / kOneRaw; }

    constexpr Fixed operator-() const { return Fixed{-raw}; }
    constexpr Fixed operator+(Fixed o) const { return Fixed{raw + o.raw}; }
    constexpr Fixed operator-(Fixed o) const { return Fixed{raw - o.raw}; }
    // Product is truncated toward negative infinity (arithmetic shift).
    constexpr Fixed operator*(Fixed o) const {
        return Fixed{static_cast<int64_t>((static_cast<i128>(raw) * o.raw) >> kFracBits)};
    }
    // Quotient is truncated toward zero. Division by zero is a logic error.
    constexpr Fixed operator/(Fixed o) const {
        return Fixed{static_cast<int64_t>((static_cast<i128>(raw) << kFracBits) / o.raw)};
    }
    constexpr Fixed operator*(int64_t i) const { return Fixed{raw * i}; }
    constexpr Fixed operator/(int64_t i) const { return Fixed{raw / i}; }

    constexpr Fixed& operator+=(Fixed o) { return *this = *this + o; }
    constexpr Fixed& operator-=(Fixed o) { return *this = *this - o; }
    constexpr Fixed& operator*=(Fixed o) { return *this = *this * o; }
    constexpr Fixed& operator/=(Fixed o) { return *this = *this / o; }

    constexpr auto operator<=>(const Fixed&) const = default;
};

constexpr Fixed abs(Fixed f) { return f.raw < 0 ? -f : f; }
constexpr Fixed min(Fixed a, Fixed b) { return a < b ? a : b; }
constexpr Fixed max(Fixed a, Fixed b) { return a < b ? b : a; }
constexpr Fixed clamp(Fixed v, Fixed lo, Fixed hi) { return min(max(v, lo), hi); }

// Square root, truncated. Negative input returns zero.
Fixed sqrt(Fixed f);

// Pi to 16 fractional bits: 205887 / 65536 = 3.14159...
inline constexpr Fixed kPi = Fixed::fromRaw(205887);
inline constexpr Fixed kHalfPi = Fixed::fromRaw(102944);
inline constexpr Fixed kTwoPi = Fixed::fromRaw(411775);

// Angles are in radians. Accurate to within a few units of the last place.
Fixed sin(Fixed radians);
Fixed cos(Fixed radians);

}  // namespace illyria::sim
