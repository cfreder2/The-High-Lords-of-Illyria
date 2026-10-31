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
    std::string gone;
    for (int t = 0; t < kUnitTypeCount; ++t)
        if (retired(static_cast<UnitType>(t))) gone += std::string(gone.empty() ? "" : ", ") + unitTypeName(static_cast<UnitType>(t));
    if (!gone.empty())
        std::printf("Retired, no longer raised anywhere (the type number is kept for old saves and sprite sheets; old saves' armies of it load as grenadiers): %s.\n\n",
                    gone.c_str());
    std::printf("| Unit | Strength | HP | Height | Radius | Mass | Speed | Melee dmg | Blow | Every | Reach | Knockback | Shield | Morale | Notes |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (retired(static_cast<UnitType>(t))) continue;
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
                "against a castle gate they do five times their blast damage (satchels three times, fireballs and Molotovs one and a half).\n"
                "A Molotov shatters where it strikes into a burst of fire, and its fire sets off satchel charges.\n\n");
    std::printf("| Unit | Missile | Wounds | Muzzle speed | Reload | Max range | Auto range | Damage | Spread at 100 m | Min range | Blast radius | Blast damage | Fuse |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (retired(static_cast<UnitType>(t))) continue;
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.ranged.muzzleSpeed == Fixed::zero()) continue;
        const char* kind = s.ranged.kind == ProjectileKind::Arrow     ? "arrow"
                           : s.ranged.kind == ProjectileKind::Bomb    ? "satchel charge"
                           : s.ranged.kind == ProjectileKind::Stone   ? "stone"
                           : s.ranged.kind == ProjectileKind::Molotov ? "Molotov"
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

    std::printf("\n## Satchel charges\n\n");
    std::printf("Carried into battle and laid on the ground, where a satchel lies still and never goes off by itself. A blast or\n"
                "a Molotov's fire reaching it sets it off a moment later, and its blast the next: chains. Left to himself a\n"
                "grenadier lays one in the path of a foe coming within 25 m (not yet within 6 m), steps back out of its blast,\n"
                "and throws his Molotovs at a satchel with foes beside it (and none of his friends) before any one man. Killed by\n"
                "fire or a blast with satchels still on him, he goes up with all of them at once: a blast a quarter wider and half\n"
                "again as hard for each one beyond the first; any other death spills them, unlit, where he falls.\n\n");
    std::printf("| Unit | Carried | Blast radius | Blast damage | Between lays | All of them at once |\n|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (retired(static_cast<UnitType>(t))) continue;
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.satchels.count == 0) continue;
        const int n = s.satchels.count;
        const Explosive& c = s.satchels.charge;
        std::printf("| %s | %d | %.1f m | %d | %.1f s | %.1f m, %d |\n", unitTypeName(static_cast<UnitType>(t)), n, d(c.radius), c.damage,
                    secs(s.satchels.layTicks), d(c.radius * (3 + n) / 4), c.damage * (1 + n) / 2);
    }

    std::printf("\n## Fliers\n\n");
    std::printf("Fliers walk by default. Moves longer than the minimum flight take a ground run, then they cruise at\n"
                "altitude over the terrain, tire, and must land to recover (rest restores stamina at half the rate\n"
                "flight spends it). They never attack from the air. Cruise heights are within a giant's reach, and\n"
                "arrows can hit them.\n\n");
    std::printf("| Unit | Ground speed | Air speed | Cruise height | Take-off run | Min flight | Endurance |\n|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (retired(static_cast<UnitType>(t))) continue;
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.flyAltitude == Fixed::zero()) continue;
        std::printf("| %s | %.1f m/s | %.0f m/s | %.1f m | %.0f m | %.0f m | %.0f s |\n", unitTypeName(static_cast<UnitType>(t)),
                    d(s.moveSpeed), d(s.flySpeed), d(s.flyAltitude), d(s.takeoffRun), d(s.minFlight), secs(s.staminaTicks));
    }

    std::printf("\n## Scavengers\n\n");
    std::printf("Left to themselves, scavengers run their own fight (`engine/sim/src/world_scavenge.cpp`; orders override it):\n"
                "round the end of the enemy's line toward its rear, taking the best thing in **sight**, darting in to throw it at\n"
                "the softest man in reach, falling back and going for the next. Prizes are worth: a satchel charge 100, a blade 60,\n"
                "a head 45, a trunk 25, a limb 18, a bone or gobbet 12, less 2 for every metre of the run. Marks, by kind:\n"
                "archers, healers and wizards first (x3), other missile troops (x2), then the rest, armoured men last (x1/2);\n"
                "nearer is better. They keep out of reach of armoured foes (a shield of 30%% or more, or a heavy body: knights,\n"
                "warriors, dwarves, cavalry, heroes, beasts), and with nothing to throw cut down the soft ones near them.\n"
                "Thrown, a part hurts as any flying part does (2 to 8 by its speed, a trunk half again), a blade cuts (5 to 14),\n"
                "a head breaks the man it strikes (his morale to nothing; the dead and heroes never break), and a satchel bursts\n"
                "where it strikes. A burning satchel is taken only with the fuse to throw it on.\n\n");
    std::printf("| Unit | Sight | Throw speed | Throw range | Darts in to | Spread at 100 m | Wind-up |\n|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (retired(static_cast<UnitType>(t))) continue;
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.scavenge.sight == Fixed::zero()) continue;
        std::printf("| %s | %.0f m | %.0f m/s | %.0f m | %.0f m | %.0f m | %.1f s |\n", unitTypeName(static_cast<UnitType>(t)), d(s.scavenge.sight),
                    d(s.scavenge.throwSpeed), d(s.scavenge.throwRange), d(s.scavenge.throwRange) * 0.6, d(s.scavenge.dispersionAt100m),
                    secs(s.scavenge.windupTicks));
    }
    std::string blades;
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (retired(static_cast<UnitType>(t)) || !bearsBlade(static_cast<UnitType>(t))) continue;
        if (!blades.empty()) blades += ", ";
        blades += unitTypeName(static_cast<UnitType>(t));
    }
    std::printf("\nBlades are taken from the bodies of: %s.\n", blades.c_str());

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
            if (retired(static_cast<UnitType>(t)) || unitStats(static_cast<UnitType>(t)).gait != gait) continue;
            if (!units.empty()) units += ", ";
            units += unitTypeName(static_cast<UnitType>(t));
        }
        std::printf(" %d%% | %s |\n", gaitClimbPct(gait), units.c_str());
    }
    return 0;
}
