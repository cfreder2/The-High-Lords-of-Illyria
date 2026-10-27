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

TEST(fixed_trig_matches_libm) {
    // libm is only the reference here; the simulation never calls it.
    for (int i = -2000; i <= 2000; i += 7) {
        const Fixed x = Fixed::fromRatio(i, 100);
        CHECK_NEAR(sin(x).toDouble(), std::sin(x.toDouble()), 1e-4);
        CHECK_NEAR(cos(x).toDouble(), std::cos(x.toDouble()), 1e-4);
    }
}
