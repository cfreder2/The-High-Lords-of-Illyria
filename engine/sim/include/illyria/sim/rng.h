#pragma once

#include <cstdint>

#include "illyria/sim/fixed.h"

namespace illyria::sim {

// PCG32 (pcg-random.org, XSH-RR variant). The only source of randomness the
// simulation may use; its state is part of the world checksum.
class Rng {
public:
    Rng() : Rng(0, 0) {}
    Rng(uint64_t seed, uint64_t stream) : inc_((stream << 1) | 1) {
        nextU32();
        state_ += seed;
        nextU32();
    }

    uint32_t nextU32() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<uint32_t>(((old >> 18) ^ old) >> 27);
        const auto rot = static_cast<uint32_t>(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    // Uniform in [0, bound) without modulo bias.
    uint32_t nextBelow(uint32_t bound) {
        const uint32_t threshold = (-bound) % bound;
        for (;;) {
            const uint32_t r = nextU32();
            if (r >= threshold) return r % bound;
        }
    }

    // Uniform in [0, 1).
    Fixed nextUnit() { return Fixed::fromRaw(nextU32() >> (32 - Fixed::kFracBits)); }
    // Uniform in [-1, 1).
    Fixed nextSigned() { return nextUnit() * 2 - Fixed::one(); }

    uint64_t state() const { return state_; }
    uint64_t increment() const { return inc_; }

private:
    uint64_t state_ = 0;
    uint64_t inc_;
};

}  // namespace illyria::sim
