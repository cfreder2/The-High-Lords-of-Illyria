#include "illyria/sim/rng.h"
#include "test.h"

using namespace illyria::sim;

TEST(rng_matches_pcg32_reference) {
    // Expected output of the reference pcg32-demo with seed 42, stream 54.
    Rng rng(42, 54);
    const uint32_t expected[] = {0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b,
                                 0xcbed606e};
    for (uint32_t e : expected) CHECK(rng.nextU32() == e);
}

TEST(rng_ranges) {
    Rng rng(7, 0);
    for (int i = 0; i < 10000; ++i) {
        CHECK(rng.nextBelow(6) < 6);
        const Fixed u = rng.nextUnit();
        CHECK(u >= Fixed::zero() && u < Fixed::one());
        const Fixed s = rng.nextSigned();
        CHECK(s >= -Fixed::one() && s < Fixed::one());
    }
}
