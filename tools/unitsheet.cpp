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
    std::printf("**Strength** is the Warlords number (1 to 9) shown on the squad's banner; each rank, hero level and blessing adds one, to 9.\n\n");
    std::printf("| Unit | Strength | HP | Height | Radius | Mass | Speed | Melee dmg | Every | Reach | Knockback | Shield | Notes |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
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
        std::printf("| %s | %d | %d | %.2f | %.2f | %.1f | %.1f | %d | %.2f s | %.1f | %.0f | %s | %s |\n",
                    unitTypeName(static_cast<UnitType>(t)), warlordsStrength(static_cast<UnitType>(t)), s.maxHp, d(s.height), d(s.radius), d(s.mass),
                    d(s.moveSpeed), s.melee.damage, secs(s.melee.cooldownTicks), d(s.melee.reach), d(s.melee.knockback),
                    s.shield.blockChance > Fixed::zero() ? (std::to_string(static_cast<int>(d(s.shield.blockChance) * 100 + 0.5)) + "%").c_str() : "",
                    notes);
    }

    std::printf("\n## Missile troops\n\n");
    std::printf("Targets beyond **max range** are approached on foot first. Idle units fire on their own inside **auto range**,\n"
                "but never at targets inside **min range**. Catapult stones fly a fixed 53 degree arc and smash what they land on;\n"
                "against a castle gate they do five times their blast damage (satchels three times, fireballs one and a half).\n\n");
    std::printf("| Unit | Missile | Muzzle speed | Reload | Max range | Auto range | Damage | Spread at 100 m | Min range | Blast radius | Blast damage | Fuse |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.ranged.muzzleSpeed == Fixed::zero()) continue;
        const char* kind = s.ranged.kind == ProjectileKind::Arrow  ? "arrow"
                           : s.ranged.kind == ProjectileKind::Bomb ? "satchel charge"
                           : s.ranged.kind == ProjectileKind::Stone ? "stone"
                                                                    : "fireball";
        const bool blast = s.ranged.kind != ProjectileKind::Arrow;
        std::printf("| %s | %s | %.0f m/s | %.1f s | %.0f m | %.0f m | %s | %.0f m | %s | %s | %s | %s |\n", unitTypeName(static_cast<UnitType>(t)),
                    kind, d(s.ranged.muzzleSpeed), secs(s.ranged.reloadTicks), d(s.ranged.maxRange), d(s.ranged.autoRange),
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
    std::printf("Speed multipliers by ground type, after Warlords' terrain: road %.2f, plains %.2f, forest %.2f, swamp %.2f,\n"
                "shallows %.2f, rock %.2f; deep water is impassable on foot. Slopes steeper than 35%% slow walkers a further 40%%.\n",
                d(groundSpeed(Ground::Road)), d(groundSpeed(Ground::Plains)), d(groundSpeed(Ground::Forest)),
                d(groundSpeed(Ground::Swamp)), d(groundSpeed(Ground::Shallows)), d(groundSpeed(Ground::Rock)));
    return 0;
}
