// Prints the locked unit sheet (engine/sim/src/units.cpp) as the Markdown
// tables in docs/UNITS.md. Regenerate the doc with:
//   ./build/tools/illyria_unitsheet > docs/UNITS.md

#include <cstdio>
#include <memory>
#include <vector>
#include <string>

#include "illyria/sim/conquest.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

using namespace illyria::sim;

namespace {
double d(Fixed f) { return f.toDouble(); }
double secs(int32_t ticks) { return static_cast<double>(ticks) / kTicksPerSecond; }

// How far a catapult's boulder rolls on from where it comes down (World::stonePath: the stone's own steps), thrown
// `away` metres over bare ground of one kind that rises `gradePct` metres in a hundred the way it is thrown.
double rollMetres(Ground ground, int64_t away, int64_t gradePct = 0) {
    constexpr int32_t kCells = 160;  // 640 m
    std::vector<Fixed> heights;
    if (gradePct != 0) {
        heights.resize(static_cast<size_t>(kCells + 1) * (kCells + 1));
        for (int64_t y = 0; y <= kCells; ++y)
            for (int64_t x = 0; x <= kCells; ++x)
                heights[static_cast<size_t>(y * (kCells + 1) + x)] = Fixed::fromRatio(x * 4 * gradePct, 100) + Fixed::fromInt(gradePct < 0 ? 200 : 0);
    }
    WorldConfig config;
    config.region = std::make_shared<const Region>(Region::fromGround(kCells, std::vector<Ground>(static_cast<size_t>(kCells) * kCells, ground), heights));
    const World w(config);
    Unit engine;
    engine.stats = unitStats(UnitType::Catapult);
    engine.pos = {Fixed::fromInt(60), Fixed::fromInt(320), Fixed::zero()};
    engine.pos.z = w.groundHeight(engine.pos.x, engine.pos.y);
    Vec3 mark = engine.pos + Vec3{Fixed::fromInt(away), Fixed::zero(), Fixed::zero()};
    mark.z = w.groundHeight(mark.x, mark.y);
    const StonePath path = w.stonePath(engine, mark);
    return path.roll.empty() ? 0 : d((path.roll.back().pos - path.landing).flat().length());
}
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
                "but never at targets inside **min range**. A catapult's boulder flies a fixed 53 degree arc, smashes what it lands on\n"
                "(its blast, below) and then rolls on over the ground, crushing the men in its way, friend and foe (see **The boulder**);\n"
                "against a castle gate its smash does nine times its blast damage (satchels three times, fireballs and Molotovs one and a half).\n"
                "A Molotov shatters where it strikes into a burst of fire, and its fire sets off satchel charges. A bowman with a\n"
                "**draw** looses his first shot at a mark only once he has drawn on it, and draws afresh whenever he leaves it\n"
                "(to march, to fight hand to hand); while he holds it, the draw comes inside his reload.\n\n");
    std::printf("| Unit | Missile | Wounds | Muzzle speed | Reload | Draw | Max range | Auto range | Damage | Spread at 100 m | Min range | Blast radius | Blast damage | Fuse |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
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
        char draw[16] = "";
        if (s.ranged.drawTicks > 0) std::snprintf(draw, sizeof draw, "%.2f s", secs(s.ranged.drawTicks));
        std::printf("| %s | %s | %s | %.0f m/s | %.1f s | %s | %.0f m | %.0f m | %s | %.0f m | %s | %s | %s | %s |\n", unitTypeName(static_cast<UnitType>(t)),
                    kind, damageTypeName(s.ranged.damageType), d(s.ranged.muzzleSpeed), secs(s.ranged.reloadTicks), draw, d(s.ranged.maxRange), d(s.ranged.autoRange),
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

    std::printf("\n## The boulder\n\n");
    std::printf("A catapult's stone is a thing on the field (`engine/sim/src/world_boulder.cpp`, docs/GAME_DESIGN.md \"The boulder\").\n"
                "It comes down with its smash (the blast above), bounces once and rolls on, %.1f m across, until the ground has\n"
                "taken its speed: far on a road, a little way in sand, mud and bog; faster and farther down a slope, soon stopped up\n"
                "one; dead in water (a splash), and all but dead against a tree, a rock, a house, a tower, a wall or a shut gate.\n"
                "Every man its middle passes within its own half width and his is struck once: hurt by 30 and 12 more for each metre a\n"
                "second it is going (to 250), thrown, and shaken (those of his side within 5 m a third as much); slower than 3 m/s it\n"
                "hurts no one. Each body takes its share of the stone's speed (a man a fifteenth, a horseman a sixth, a giant a third,\n"
                "an elephant near half, an engine two thirds), and a body of mass 4 or more that leaves it under 3 m/s has stopped it.\n"
                "It knows no side. Rolled into a gate it does half the blow of its speed (a stone from the air is 24 m/s). Where it\n"
                "stops it lies, in nobody's way.\n\n",
                d(kStoneRadius * 2));
    std::printf("How far it rolls on from where it comes down, over bare level ground (the hop included), by how far it was thrown:\n\n");
    std::printf("| Ground | Thrown 40 m | 100 m | 150 m |\n|---|---|---|---|\n");
    {
        const struct {
            const char* name;
            Ground ground;
        } kinds[] = {{"Road, bridge", Ground::Road}, {"Plains", Ground::Plains}, {"Rock", Ground::Rock}, {"Forest (between the trees)", Ground::Forest},
                     {"Sand", Ground::Sand},         {"Mud", Ground::Mud},       {"Swamp", Ground::Swamp}};
        for (const auto& k : kinds) std::printf("| %s | %.0f m | %.0f m | %.0f m |\n", k.name, rollMetres(k.ground, 40), rollMetres(k.ground, 100), rollMetres(k.ground, 150));
        std::printf("| Shallows, deep water | stops | stops | stops |\n");
    }
    std::printf("\nOn plains, thrown 100 m along a slope (metres of rise in a hundred, the way it is thrown):\n\n");
    std::printf("| Up 25 | Up 15 | Up 8 | Level | Down 8 | Down 15 | Down 25 | Down 35 |\n|---|---|---|---|---|---|---|---|\n|");
    for (const int64_t grade : {25, 15, 8, 0, -8, -15, -25}) std::printf(" %.0f m |", rollMetres(Ground::Plains, 100, grade));
    std::printf(" to the foot of the hill (%.0f m here) |\n", rollMetres(Ground::Plains, 100, -35));
    std::printf("\n(On grass a slope steeper than about 28 in a hundred pulls it harder than the ground holds it: it gathers speed, to 32 m/s at most.)\n");

    std::printf("\n## Fliers\n\n");
    std::printf("Fliers walk by default. Moves longer than the minimum flight take a ground run, then they cruise at\n"
                "altitude over the terrain, tire, and must land to recover (rest restores stamina at half the rate\n"
                "flight spends it). They never attack from the air. Cruise heights are within a giant's reach, and\n"
                "arrows can hit them. On the conquest map a stack of fliers alone moves at the **long-run pace** of\n"
                "that cycle, whatever is below: the take-off run (at half again its walking pace), a flight as long as\n"
                "its endurance, then twice as long resting (`Conquest::flightPace`). A stack with a walker in it walks.\n\n");
    std::printf("| Unit | Ground speed | Air speed | Cruise height | Take-off run | Min flight | Endurance | Long-run pace |\n|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (retired(static_cast<UnitType>(t))) continue;
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.flyAltitude == Fixed::zero()) continue;
        std::printf("| %s | %.1f m/s | %.0f m/s | %.1f m | %.0f m | %.0f m | %.0f s | %.2f m/s |\n", unitTypeName(static_cast<UnitType>(t)),
                    d(s.moveSpeed), d(s.flySpeed), d(s.flyAltitude), d(s.takeoffRun), d(s.minFlight), secs(s.staminaTicks),
                    Conquest::flightPace(static_cast<UnitType>(t)) / 1000.0);
    }

    std::printf("\n## Scavengers\n\n");
    std::printf("Scavengers are soldiers like any other until ordered to raid (`engine/sim/src/world_scavenge.cpp`): they go\n"
                "where they are sent, hold there and fight what comes at them. Let loose (the Raid order), each runs his own fight\n"
                "from where he stands, knowing only what is within his **sight**: he takes the best thing lying there, darts in to\n"
                "throw it at a soft man or a straggler, falls back to one place well clear, and goes again. The raid ends at\n"
                "another order, when his pack has lost half its men, or after 15 s with nothing worth taking in sight.\n"
                "Prizes are worth: a satchel charge 100, a blade 60, a head 45, a trunk 25, a limb 18, a bone or gobbet 12, less\n"
                "2 for every metre of the run. Marks, by kind: archers, healers, wizards and grenadiers first (x3), other missile\n"
                "troops (x2), then the rest, armoured men last (x0.3); the wounded up to twice; the straggler far from his\n"
                "friends up to x2.5; a shield facing them less its chance to block; nearer is better. He goes only when his pack\n"
                "about him (each at half again his fighting power) is a match for the foes within 10 m of the man (the armoured,\n"
                "those quick enough to catch him and braced pikes counting double or more) and for the missile troops whose reach\n"
                "covers where he would stand (at twice their power, by how deep in it): 55 in 100 to throw, 60 to cut a man down,\n"
                "breaking off below 45. Out of missile reach first. He keeps out of reach of armoured foes (a shield of 30%% or more,\n"
                "or a heavy body: knights, warriors, cavalry, heroes, beasts). Thrown, a part hurts as any flying part does (2 to 8\n"
                "by its speed, a trunk half again), a blade cuts (5 to 14), a head breaks the man it strikes (his morale to\n"
                "nothing; the dead and heroes never break), and a satchel bursts where it strikes; a shield facing the throw\n"
                "catches a part or a blade. A burning satchel is taken only with the fuse to throw it on. A soldier with no order\n"
                "does not chase a scavenger quicker than him; struck by a throw, he turns his shield to the thrower.\n\n");
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
                "slows it more than the detour does. A road doubles every gait's pace. A bridge is walked at the pace\n"
                "of the fields: Bridge is the ground of every deck, a castle's walls and ramps as well as the bridges\n"
                "over rivers. Deep water is impassable on foot. Climbing divides speed by 1 + grade x 2.5 x the gait's\n"
                "climb percentage; going down is up to 15%% quicker; slopes steeper than 35%% slow walkers a further\n"
                "20%%. Fliers walk like men. The conquest map marches by the same table and the same climbs, cell by\n"
                "cell (`engine/sim/src/conquest_march.cpp`): a stack at the pace of its slowest army, a road cell as\n"
                "road, a hill as plains to be climbed, marsh as swamp, mountains and water closed.\n\n");
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
