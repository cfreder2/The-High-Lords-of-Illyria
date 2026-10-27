#pragma once

#include <cstdint>

namespace illyria::sim {

// FNV-1a over explicitly little-endian bytes, so checksums agree across
// platforms. Used for per-tick desync detection between lockstep peers.
class StateHasher {
public:
    void addU64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            hash_ ^= (v >> (i * 8)) & 0xFF;
            hash_ *= 0x100000001B3ULL;
        }
    }
    void addI64(int64_t v) { addU64(static_cast<uint64_t>(v)); }

    uint64_t value() const { return hash_; }

private:
    uint64_t hash_ = 0xCBF29CE484222325ULL;
};

}  // namespace illyria::sim
