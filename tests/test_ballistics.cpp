#include "illyria/sim/ballistics.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Fixed kGravity = Fixed::fromRatio(981, 100);

Vec3 v(int64_t x, int64_t y, int64_t z) {
    return {Fixed::fromInt(x), Fixed::fromInt(y), Fixed::fromInt(z)};
}

// Flies a projectile with the same integrator as the world until it comes
// down through the target's height, and returns the horizontal miss distance.
double simulatedMiss(const Vec3& origin, const Vec3& target, const Vec3& vel0) {
    Vec3 p = origin, vel = vel0;
    const Vec3 accel{Fixed::zero(), Fixed::zero(), -kGravity};
    for (int i = 0; i < 100 * kTicksPerSecond; ++i) {
        const Vec3 prev = p;
        p += vel * kTickDt + accel * (kTickDt * kTickDt / 2);
        vel += accel * kTickDt;
        if (vel.z < Fixed::zero() && p.z <= target.z) {
            const Fixed t = (prev.z - target.z) / (prev.z - p.z);
            return (prev + (p - prev) * t - target).flat().length().toDouble();
        }
    }
    return 1e9;
}
}  // namespace

TEST(ballistics_hits_targets_in_range) {
    const Fixed speed = Fixed::fromInt(55);
    const Vec3 origin = v(0, 0, 2);
    const Vec3 targets[] = {v(10, 0, 0), v(60, 25, 0), v(150, -40, 0), v(200, 0, 12), v(-80, 80, -5)};
    for (const Vec3& t : targets) {
        const LaunchSolution s = solveLowArc(origin, t, speed, kGravity);
        CHECK(s.inRange);
        CHECK_NEAR(s.velocity.length().toDouble(), 55.0, 0.01);
        CHECK_NEAR(simulatedMiss(origin, t, s.velocity), 0.0, 0.05);
    }
}

TEST(ballistics_out_of_range_fires_max_range_arc) {
    const Fixed speed = Fixed::fromInt(30);  // max range ~92 m on flat ground
    const LaunchSolution s = solveLowArc(v(0, 0, 0), v(0, 400, 0), speed, kGravity);
    CHECK(!s.inRange);
    CHECK(s.velocity.x == Fixed::zero());
    CHECK(s.velocity.y > Fixed::zero());
    CHECK_NEAR(s.velocity.y.toDouble(), s.velocity.z.toDouble(), 1e-4);
}
