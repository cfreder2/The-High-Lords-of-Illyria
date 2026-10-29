// Prints the locked unit sheet (engine/sim/src/units.cpp) as the Markdown
// tables in docs/UNITS.md. Regenerate the doc with:
//   ./build/tools/illyria_unitsheet > docs/UNITS.md

#include <cstdio>
#include <string>

#include "illyria/sim/units.h"

using namespace illyria::sim;

namespace {
double d(Fixed f) { return f.toDouble(); }
double secs(int32_t ticks) { return static_cast<double>(ticks) / kTicksPerSecond; }
}  // namespace

int main() {
    std::printf("# Unit Sheet\n\n");
    std::printf("The locked stats for every unit type, generated from `engine/sim/src/units.cpp` by\n"
                "`tools/unitsheet.cpp`. A checksum test (`unit_sheet_is_locked`) fails on any change, so\n"
                "balance changes are always deliberate: change the code, regenerate this file, update the\n"
                "checksum, all in one commit.\n\n");
    std::printf("Scale: metres and seconds. A man is 1.8 m tall; a steady run is about 2.2 m/s and a gallop 7 m/s.\n\n");

    std::printf("## Body and melee\n\n");
    std::printf("**Strength** is the Warlords number (1 to 9) shown on the squad's banner; each hero level and blessing adds one, to 9 (a soldier's rank is a small edge instead: +3%% a rank, docs/GAME_DESIGN.md §3.4).\n\n");
    std::printf("**Morale** is how hard shocks shake the type, in percent of a man's (lower is steadier); \"never\" types never break.\n\n");
    std::printf("| Unit | Strength | HP | Height | Radius | Mass | Speed | Melee dmg | Blow | Every | Reach | Knockback | Shield | Morale | Notes |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        char notes[160] = "";
        int n = 0;
        if (s.mounted) n += std::snprintf(notes + n, sizeof notes - n, "mounted; ");
        if (!s.bleeds) n += std::snprintf(notes + n, sizeof notes - n, "no blood; ");
        if (s.melee.bonusVsMountedPct) n += std::snprintf(notes + n, sizeof notes - n, "+%d%% vs mounted; ", s.melee.bonusVsMountedPct);
        if (s.terrain.openPct) n += std::snprintf(notes + n, sizeof notes - n, "+%d%% in the open; ", s.terrain.openPct);
        if (s.terrain.hillsPct) n += std::snprintf(notes + n, sizeof notes - n, "+%d%% in hills; ", s.terrain.hillsPct);
        if (s.healing.amount)
            n += std::snprintf(notes + n, sizeof notes - n, "heals %d every %.1f s, %d charges; ", s.healing.amount,
                               secs(s.healing.cooldownTicks), s.healing.charges);
        const std::string morale = s.moraleShockPct == 0 ? "never" : std::to_string(s.moraleShockPct) + "%";
        std::printf("| %s | %d | %d | %.2f | %.2f | %.1f | %.1f | %d | %s | %.2f s | %.1f | %.0f | %s | %s | %s |\n",
                    unitTypeName(static_cast<UnitType>(t)), warlordsStrength(static_cast<UnitType>(t)), s.maxHp, d(s.height), d(s.radius), d(s.mass),
                    d(s.moveSpeed), s.melee.damage, s.melee.damage ? damageTypeName(s.melee.damageType) : "", secs(s.melee.cooldownTicks), d(s.melee.reach), d(s.melee.knockback),
                    s.shield.blockChance > Fixed::zero() ? (std::to_string(static_cast<int>(d(s.shield.blockChance) * 100 + 0.5)) + "%").c_str() : "",
                    morale.c_str(), notes);
    }

    std::printf("\n## Missile troops\n\n");
    std::printf("Targets beyond **max range** are approached on foot first. Idle units fire on their own inside **auto range**,\n"
                "but never at targets inside **min range**. Catapult stones fly a fixed 53 degree arc and smash what they land on;\n"
                "against a castle gate they do five times their blast damage (satchels three times, fireballs one and a half).\n\n");
    std::printf("| Unit | Missile | Wounds | Muzzle speed | Reload | Max range | Auto range | Damage | Spread at 100 m | Min range | Blast radius | Blast damage | Fuse |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.ranged.muzzleSpeed == Fixed::zero()) continue;
        const char* kind = s.ranged.kind == ProjectileKind::Arrow  ? "arrow"
                           : s.ranged.kind == ProjectileKind::Bomb ? "satchel charge"
                           : s.ranged.kind == ProjectileKind::Stone ? "stone"
                                                                    : "fireball";
        const bool blast = s.ranged.kind != ProjectileKind::Arrow;
        std::printf("| %s | %s | %s | %.0f m/s | %.1f s | %.0f m | %.0f m | %s | %.0f m | %s | %s | %s | %s |\n", unitTypeName(static_cast<UnitType>(t)),
                    kind, damageTypeName(s.ranged.damageType), d(s.ranged.muzzleSpeed), secs(s.ranged.reloadTicks), d(s.ranged.maxRange), d(s.ranged.autoRange),
                    blast ? "" : std::to_string(s.ranged.damage).c_str(), d(s.ranged.dispersionAt100m),
                    s.ranged.minRange > Fixed::zero() ? (std::to_string(static_cast<int>(d(s.ranged.minRange))) + " m").c_str() : "",
                    blast ? (std::to_string(static_cast<int>(d(s.ranged.explosive.radius) * 10) / 10.0).substr(0, 3) + " m").c_str() : "",
                    blast ? std::to_string(s.ranged.explosive.damage).c_str() : "",
                    s.ranged.explosive.fuseTicks ? (std::to_string(static_cast<int>(secs(s.ranged.explosive.fuseTicks))) + " s").c_str() : "");
    }

    std::printf("\n## Fliers\n\n");
    std::printf("Fliers walk by default. Moves longer than the minimum flight take a ground run, then they cruise at\n"
                "altitude over the terrain, tire, and must land to recover (rest restores stamina at half the rate\n"
                "flight spends it). They never attack from the air. Cruise heights are within a giant's reach, and\n"
                "arrows can hit them.\n\n");
    std::printf("| Unit | Ground speed | Air speed | Cruise height | Take-off run | Min flight | Endurance |\n|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.flyAltitude == Fixed::zero()) continue;
        std::printf("| %s | %.1f m/s | %.0f m/s | %.1f m | %.0f m | %.0f m | %.0f s |\n", unitTypeName(static_cast<UnitType>(t)),
                    d(s.moveSpeed), d(s.flySpeed), d(s.flyAltitude), d(s.takeoffRun), d(s.minFlight), secs(s.staminaTicks));
    }

    std::printf("\n## Ground\n\n");
    std::printf("Terrain is tactical. A unit moves at its speed times its gait's multiplier for the ground under it\n"
                "(`engine/sim/src/gait.cpp`), and its paths cost travel time with the same table, so each kind takes its\n"
                "own fastest way: to the road and along it rather than straight over the fields, round a wood that\n"
                "slows it more than the detour does. Deep water is impassable on foot. Climbing divides speed by\n"
                "1 + grade x 2.5 x the gait's climb percentage; going down is up to 15%% quicker; slopes steeper than\n"
                "35%% slow walkers a further 20%%. Fliers walk like men.\n\n");
    const Ground grounds[] = {Ground::Road, Ground::Plains, Ground::Forest, Ground::Swamp, Ground::Shallows,
                              Ground::Rock, Ground::Mud, Ground::Sand, Ground::Bridge};
    std::printf("| Gait | Road | Plains | Forest | Swamp | Shallows | Rock | Mud | Sand | Bridge | Climb | Units |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int g = 0; g < kGaitCount; ++g) {
        const Gait gait = static_cast<Gait>(g);
        std::printf("| %s |", gaitName(gait));
        for (Ground gr : grounds) std::printf(" %.2f |", d(gaitSpeed(gait, gr)));
        std::string units;
        for (int t = 0; t < kUnitTypeCount; ++t) {
            if (unitStats(static_cast<UnitType>(t)).gait != gait) continue;
            if (!units.empty()) units += ", ";
            units += unitTypeName(static_cast<UnitType>(t));
        }
        std::printf(" %d%% | %s |\n", gaitClimbPct(gait), units.c_str());
    }
    return 0;
}
