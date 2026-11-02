#include "illyria/sim/gait.h"

#include "illyria/sim/region.h"

namespace illyria::sim {

namespace {

// Percent of a unit's speed on each ground, in Ground's order:
// plains, road, forest, swamp, shallows, deep water, rock, bridge, mud, sand.
// A road doubles every gait's pace, horse and wheel alike (docs/ONE_WORLD.md).
// A bridge stays at a walk: Bridge is the ground of every deck, a castle's
// walls and ramps as well as the bridges over rivers (Region::fromContinent),
// so at a road's pace men on the walls would run along them. A road bridge's
// deck, 20 to 40 m, is crossed at the pace of the open fields.
constexpr int32_t kSpeedPct[kGaitCount][kGroundCount] = {
    {100, 200, 70, 50, 40, 0, 50, 100, 55, 85},   // Foot
    {100, 200, 50, 30, 35, 0, 40, 100, 40, 80},   // Horse
    {100, 200, 85, 35, 35, 0, 55, 100, 45, 80},   // Wolf
    {100, 200, 70, 50, 35, 0, 80, 100, 55, 85},   // Dwarf
    {100, 200, 95, 55, 40, 0, 50, 100, 55, 85},   // Woodland
    {100, 200, 65, 60, 65, 0, 60, 100, 90, 90},   // Giant
    {100, 200, 70, 100, 55, 0, 50, 100, 80, 85},  // Undead
    {100, 200, 40, 25, 30, 0, 30, 100, 30, 60},   // Wheeled
    {100, 200, 90, 65, 50, 0, 80, 100, 75, 90},   // Hyena
};
constexpr int32_t kClimbPct[kGaitCount] = {100, 120, 90, 50, 100, 80, 100, 150, 70};

}  // namespace

Fixed gaitSpeed(Gait gait, Ground ground) {
    const int gi = static_cast<int>(gait), g = static_cast<int>(ground);
    if (gi < 0 || gi >= kGaitCount || g < 0 || g >= kGroundCount) return Fixed::one();
    return Fixed::fromRatio(kSpeedPct[gi][g], 100);
}

int32_t gaitClimbPct(Gait gait) {
    const int gi = static_cast<int>(gait);
    return gi >= 0 && gi < kGaitCount ? kClimbPct[gi] : 100;
}

const char* gaitName(Gait gait) {
    switch (gait) {
        case Gait::Foot: return "Foot";
        case Gait::Horse: return "Horse";
        case Gait::Wolf: return "Wolf";
        case Gait::Dwarf: return "Dwarf";
        case Gait::Woodland: return "Woodland";
        case Gait::Giant: return "Giant";
        case Gait::Undead: return "Undead";
        case Gait::Wheeled: return "Wheeled";
        case Gait::Hyena: return "Hyena";
        case Gait::Count: break;
    }
    return "?";
}

}  // namespace illyria::sim
