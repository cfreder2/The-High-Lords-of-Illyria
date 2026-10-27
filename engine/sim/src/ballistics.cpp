#include "illyria/sim/ballistics.h"

namespace illyria::sim {

namespace {
// cos(45 deg) == sin(45 deg)
constexpr Fixed kInvSqrt2 = Fixed::fromRaw(46341);
}  // namespace

LaunchSolution solveLowArc(const Vec3& origin, const Vec3& target, Fixed speed, Fixed gravity) {
    const Vec3 d = target - origin;
    const Fixed distSq = d.lengthSq();
    if (distSq == Fixed::zero()) return {{Fixed::zero(), Fixed::zero(), speed}, true};

    // Constant-acceleration flight for time T must satisfy
    //   u = d/T + (0, 0, g*T/2)  and  |u| == speed,
    // which is a quadratic in s = T^2:
    //   (g^2/4) s^2 + (g*h - v^2) s + |d|^2 == 0.
    // The smaller root is the low arc; written as 2|d|^2 / (b + sqrt(disc)) to
    // avoid cancellation at short range.
    const Fixed b = speed * speed - gravity * d.z;
    const Fixed disc = b * b - gravity * gravity * distSq;
    if (b <= Fixed::zero() || disc < Fixed::zero()) {
        const Vec3 flat = d.flat();
        const Fixed flatLen = flat.length();
        if (flatLen == Fixed::zero()) return {{Fixed::zero(), Fixed::zero(), speed}, false};
        const Vec3 dir = flat / flatLen;
        return {dir * (speed * kInvSqrt2) + Vec3{Fixed::zero(), Fixed::zero(), speed * kInvSqrt2},
                false};
    }

    const Fixed s = distSq * 2 / (b + sqrt(disc));
    const Fixed t = sqrt(s);
    if (t == Fixed::zero()) return {d, true};
    return {d / t + Vec3{Fixed::zero(), Fixed::zero(), gravity * t / 2}, true};
}

}  // namespace illyria::sim
