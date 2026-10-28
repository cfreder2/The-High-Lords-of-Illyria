#include "illyria/sim/units.h"

namespace illyria::sim {

// The locked unit sheet. Every number here is documented in docs/UNITS.md and
// pinned by a checksum test, so balance changes are always deliberate.
//
// Scale: 1.0 == 1 metre, a man is 1.8 m tall. Speeds in m/s (a soldier's
// steady run is ~2.2, a galloping horse ~7). Times via sec(): ticks at 30 Hz.
// A rank-and-file soldier has 45-90 hit points and dies to 3-6 blows.

namespace {

constexpr Fixed m(int64_t v) { return Fixed::fromInt(v); }
constexpr Fixed r(int64_t num, int64_t den) { return Fixed::fromRatio(num, den); }
constexpr int32_t sec(int32_t num, int32_t den = 1) { return kTicksPerSecond * num / den; }

UnitStats base(UnitType type, Fixed radius, Fixed height, Fixed speed, int32_t hp) {
    UnitStats s;
    s.type = static_cast<uint8_t>(type);
    s.radius = radius;
    s.height = height;
    s.moveSpeed = speed;
    s.maxHp = hp;
    s.aggroRange = m(20);
    return s;
}

void melee(UnitStats& s, int32_t damage, Fixed reach, int32_t cooldownTicks, Fixed knockback = {}) {
    s.melee.damage = damage;
    s.melee.reach = reach;
    s.melee.cooldownTicks = cooldownTicks;
    s.melee.knockback = knockback;
}

void bow(UnitStats& s, Fixed speed, int32_t reloadTicks, int32_t damage, Fixed dispersion, Fixed autoRange,
         Fixed maxRange) {
    s.ranged.muzzleSpeed = speed;
    s.ranged.reloadTicks = reloadTicks;
    s.ranged.damage = damage;
    s.ranged.dispersionAt100m = dispersion;
    s.ranged.autoRange = autoRange;
    s.ranged.maxRange = maxRange;
    s.ranged.kind = ProjectileKind::Arrow;
}

void explosive(UnitStats& s, ProjectileKind kind, Fixed speed, int32_t reloadTicks, Fixed dispersion, Fixed autoRange,
               Fixed maxRange, Fixed radius, int32_t damage, Fixed impulse, int32_t fuseTicks = 0) {
    s.ranged.muzzleSpeed = speed;
    s.ranged.reloadTicks = reloadTicks;
    s.ranged.dispersionAt100m = dispersion;
    s.ranged.autoRange = autoRange;
    s.ranged.maxRange = maxRange;
    s.ranged.kind = kind;
    s.ranged.explosive = {radius, damage, impulse, fuseTicks, r(7, 20)};
}

void flier(UnitStats& s, Fixed altitude, Fixed airSpeed, Fixed takeoffRun, int32_t enduranceTicks) {
    s.flyAltitude = altitude;
    s.flySpeed = airSpeed;
    s.takeoffRun = takeoffRun;
    s.minFlight = m(50);
    s.staminaTicks = enduranceTicks;
}

void shield(UnitStats& s, Fixed blockChance) { s.shield.blockChance = blockChance; }

// What each type's weapons do to flesh (the death matrix, world_death.cpp).
DamageType meleeDamageType(UnitType type) {
    using T = UnitType;
    switch (type) {
        case T::Pikemen:
        case T::HeavyCavalry:  // the lance
        case T::Spiders:       // fangs
        case T::Unicorns:      // the horn
            return DamageType::Pierce;
        case T::Dwarves:  // axes
        case T::Minotaurs:
        case T::Dragons:
        case T::Devils:
        case T::Demons:
        case T::Berserker:  // two-handed swords
            return DamageType::Cleave;
        case T::Giants:  // clubs
        case T::Elephants:
        case T::Wizards:  // staves
        case T::DwarfGrenadier:
        case T::Journeyman:
            return DamageType::Blunt;
        default: return DamageType::Slash;  // swords, sabres, claws
    }
}

DamageType missileDamageType(ProjectileKind kind) {
    switch (kind) {
        case ProjectileKind::Bomb: return DamageType::Explosive;
        case ProjectileKind::Fireball: return DamageType::Fire;
        case ProjectileKind::Stone: return DamageType::Blunt;
        default: return DamageType::Pierce;
    }
}

}  // namespace

// Fliers walk like men.
Gait gaitOf(UnitType type) {
    using T = UnitType;
    switch (type) {
        case T::LightCavalry:
        case T::HeavyCavalry:
        case T::Unicorns: return Gait::Horse;
        case T::WolfRiders: return Gait::Wolf;
        case T::Dwarves:
        case T::DwarfGrenadier: return Gait::Dwarf;
        case T::Archers:
        case T::Spiders: return Gait::Woodland;
        case T::Giants:
        case T::Minotaurs:
        case T::Elephants:
        case T::Devils:
        case T::Demons: return Gait::Giant;
        case T::Undead: return Gait::Undead;
        case T::Catapult: return Gait::Wheeled;
        default: return Gait::Foot;
    }
}

// How hard shocks shake each type (percent of a man's; 0 never breaks): the
// mindless dead and heroes never run; fiends and dragons all but never;
// berserkers are past caring; elephants panic.
int32_t moraleShockPct(UnitType type) {
    using T = UnitType;
    switch (type) {
        case T::LightInfantry: return 110;
        case T::HeavyInfantry: return 85;
        case T::Archers: return 110;
        case T::Pikemen: return 90;
        case T::LightCavalry: return 100;
        case T::HeavyCavalry: return 80;
        case T::Dwarves: return 75;
        case T::WolfRiders: return 100;
        case T::Giants: return 60;
        case T::Minotaurs: return 60;
        case T::Spiders: return 70;
        case T::Griffins: return 70;
        case T::Pegasi: return 90;
        case T::Elephants: return 120;
        case T::Unicorns: return 70;
        case T::Wizards: return 100;
        case T::Dragons: return 25;
        case T::Devils: return 20;
        case T::Demons: return 30;
        case T::Undead: return 0;
        case T::Archons: return 30;
        case T::Hero: return 0;
        case T::DwarfGrenadier: return 100;
        case T::Journeyman: return 120;
        case T::Warrior: return 85;
        case T::Berserker: return 35;
        case T::Catapult: return 110;
        case T::Count: break;
    }
    return 100;
}

UnitStats unitStats(UnitType type) {
    using T = UnitType;
    UnitStats s;
    switch (type) {
        case T::LightInfantry:
            s = base(type, r(2, 5), r(9, 5), r(12, 5), 60);
            melee(s, 11, r(3, 5), sec(13, 10));
            shield(s, r(1, 4));
            break;
        case T::HeavyInfantry:
            s = base(type, r(9, 20), r(19, 10), r(19, 10), 90);
            melee(s, 15, r(3, 5), sec(3, 2));
            shield(s, r(11, 20));
            s.mass = r(13, 10);
            break;
        case T::Archers:  // Warlords' Elven Archers
            s = base(type, r(2, 5), r(9, 5), r(12, 5), 45);
            bow(s, m(42), sec(4), 16, m(6), m(55), m(70));
            melee(s, 5, r(1, 2), sec(6, 5));
            s.aggroRange = m(6);
            break;
        case T::Pikemen:
            s = base(type, r(2, 5), r(9, 5), m(2), 65);
            melee(s, 14, r(11, 5), sec(8, 5));
            s.melee.bonusVsMountedPct = 150;
            s.terrain.openPct = 20;  // Warlords II: +1 in the open
            break;
        case T::LightCavalry:
            s = base(type, r(9, 10), r(5, 2), m(7), 85);
            melee(s, 13, r(4, 5), sec(6, 5), m(3));
            s.mounted = true;
            s.mass = m(3);
            s.aggroRange = m(30);
            s.terrain.openPct = 20;  // +1 in the open
            break;
        case T::HeavyCavalry:
            s = base(type, r(19, 20), r(27, 10), r(11, 2), 130);
            melee(s, 20, r(4, 5), sec(3, 2), m(5));
            shield(s, r(2, 5));
            s.mounted = true;
            s.mass = m(4);
            s.aggroRange = m(30);
            s.terrain.openPct = 40;  // +2 in the open
            break;
        case T::Dwarves:  // Warlords' Dwarven Legions: axes and shields
            s = base(type, r(9, 20), r(27, 20), r(17, 10), 90);
            melee(s, 17, r(1, 2), sec(3, 2));
            shield(s, r(3, 10));
            s.mass = r(7, 5);
            s.terrain.hillsPct = 25;  // +1 in the hills
            break;
        case T::WolfRiders:
            s = base(type, r(4, 5), r(19, 10), r(15, 2), 75);
            melee(s, 12, r(3, 5), sec(11, 10), m(2));
            s.mounted = true;
            s.mass = m(2);
            s.aggroRange = m(30);
            break;
        case T::Giants:
            s = base(type, r(6, 5), r(11, 2), r(12, 5), 350);
            melee(s, 45, r(8, 5), sec(11, 5), m(12));
            s.mass = m(8);
            s.aggroRange = m(25);
            break;
        case T::Minotaurs:
            s = base(type, r(9, 10), m(3), m(3), 230);
            melee(s, 32, r(1, 1), sec(17, 10), m(8));
            s.mass = m(4);
            break;
        case T::Spiders:
            s = base(type, r(11, 10), r(13, 10), m(5), 120);
            melee(s, 18, r(3, 5), sec(1));
            s.mass = m(2);
            s.aggroRange = m(25);
            break;
        case T::Griffins:
            s = base(type, r(6, 5), r(11, 5), m(3), 150);
            melee(s, 24, r(4, 5), sec(6, 5), m(4));
            flier(s, r(13, 2), m(12), m(14), sec(30));
            s.mass = m(3);
            s.aggroRange = m(30);
            break;
        case T::Pegasi:  // winged horses with riders
            s = base(type, r(11, 10), r(23, 10), r(9, 2), 95);
            melee(s, 14, r(4, 5), sec(6, 5));
            flier(s, m(7), m(14), m(18), sec(40));
            s.mounted = true;
            s.mass = r(5, 2);
            s.aggroRange = m(30);
            break;
        case T::Elephants:
            s = base(type, r(9, 5), m(4), r(16, 5), 450);
            melee(s, 38, r(6, 5), sec(11, 5), m(14));
            s.mounted = true;
            s.mass = m(12);
            break;
        case T::Unicorns:
            s = base(type, r(9, 10), r(11, 5), m(7), 115);
            melee(s, 24, r(1, 1), sec(13, 10), m(3));
            s.mass = m(3);
            s.aggroRange = m(30);
            break;
        case T::Wizards:
            s = base(type, r(2, 5), r(19, 10), m(2), 45);
            explosive(s, ProjectileKind::Fireball, m(24), sec(9), m(4), m(45), m(50), r(7, 2), 40, m(8));
            melee(s, 4, r(1, 2), sec(6, 5));
            s.aggroRange = m(5);
            break;
        case T::Dragons:
            s = base(type, r(5, 2), r(9, 2), r(5, 2), 650);
            explosive(s, ProjectileKind::Fireball, m(30), sec(6), m(4), m(50), m(60), m(5), 50, m(10));
            melee(s, 40, r(6, 5), sec(9, 5), m(10));
            flier(s, m(10), m(15), m(30), sec(25));
            s.mass = m(15);
            s.aggroRange = m(8);
            break;
        case T::Devils:
            s = base(type, r(1, 1), m(3), r(16, 5), 270);
            melee(s, 36, r(1, 1), sec(3, 2), m(6));
            s.mass = m(4);
            s.aggroRange = m(25);
            break;
        case T::Demons:
            s = base(type, r(11, 10), r(18, 5), r(17, 5), 320);
            melee(s, 40, r(6, 5), sec(8, 5), m(8));
            s.mass = m(5);
            s.aggroRange = m(25);
            break;
        case T::Undead:
            s = base(type, r(2, 5), r(9, 5), r(8, 5), 70);
            melee(s, 11, r(3, 5), sec(7, 5));
            s.bleeds = false;
            s.aggroRange = m(30);
            break;
        case T::Archons:  // winged angels
            s = base(type, r(4, 5), r(12, 5), r(14, 5), 170);
            melee(s, 26, r(4, 5), sec(6, 5), m(3));
            flier(s, m(6), m(11), m(10), sec(35));
            s.mass = m(2);
            s.aggroRange = m(30);
            break;
        case T::Hero:
            s = base(type, r(1, 2), m(2), r(14, 5), 260);
            melee(s, 30, r(4, 5), sec(11, 10), m(3));
            shield(s, r(11, 20));
            s.mass = m(2);
            s.aggroRange = m(25);
            break;
        case T::DwarfGrenadier:  // Myth's dwarves: satchel charges that bounce, roll and explode
            s = base(type, r(9, 20), r(13, 10), r(9, 5), 50);
            explosive(s, ProjectileKind::Bomb, m(14), sec(6), m(10), m(16), m(19), m(4), 80, m(9), sec(2));
            melee(s, 5, r(1, 2), sec(6, 5));
            s.aggroRange = m(4);
            break;
        case T::Journeyman:  // Myth's healer
            s = base(type, r(2, 5), r(9, 5), r(23, 10), 50);
            s.healing = {35, r(3, 2), sec(5, 2), 6};
            melee(s, 6, r(1, 2), sec(6, 5));
            s.aggroRange = m(5);
            break;
        case T::Warrior:  // Myth's sword-and-shield infantry
            s = base(type, r(9, 20), r(19, 10), r(11, 5), 80);
            melee(s, 17, r(3, 5), sec(6, 5));
            shield(s, r(13, 20));
            s.mass = r(6, 5);
            break;
        case T::Berserker:  // Myth's berserks: fast, hard-hitting, no shield
            s = base(type, r(9, 20), r(19, 10), r(18, 5), 85);
            melee(s, 25, r(3, 5), sec(19, 20));
            s.mass = r(6, 5);
            s.aggroRange = m(30);
            break;
        case T::Catapult:  // a wheeled mangonel and its crew: a 15 kg stone up to 150 m
            s = base(type, r(8, 5), r(5, 2), r(4, 5), 380);
            explosive(s, ProjectileKind::Stone, m(40), sec(14), m(4), m(140), m(150), m(3), 100, m(9));
            s.ranged.minRange = m(30);
            s.aggroRange = Fixed::zero();
            s.mass = m(25);
            s.bleeds = false;
            break;
        case T::Count:
            break;
    }
    s.gait = gaitOf(type);
    s.moraleShockPct = moraleShockPct(type);
    s.melee.damageType = meleeDamageType(type);
    s.ranged.damageType = missileDamageType(s.ranged.kind);
    return s;
}

const char* unitTypeName(UnitType type) {
    using T = UnitType;
    switch (type) {
        case T::LightInfantry: return "Light Infantry";
        case T::HeavyInfantry: return "Heavy Infantry";
        case T::Archers: return "Elven Archers";
        case T::Pikemen: return "Pikemen";
        case T::LightCavalry: return "Light Cavalry";
        case T::HeavyCavalry: return "Heavy Cavalry";
        case T::Dwarves: return "Dwarven Legion";
        case T::WolfRiders: return "Wolf Riders";
        case T::Giants: return "Giants";
        case T::Minotaurs: return "Minotaurs";
        case T::Spiders: return "Giant Spiders";
        case T::Griffins: return "Griffins";
        case T::Pegasi: return "Pegasi";
        case T::Elephants: return "Elephants";
        case T::Unicorns: return "Unicorns";
        case T::Wizards: return "Wizards";
        case T::Dragons: return "Dragons";
        case T::Devils: return "Devils";
        case T::Demons: return "Demons";
        case T::Undead: return "Undead";
        case T::Archons: return "Archons";
        case T::Hero: return "Hero";
        case T::DwarfGrenadier: return "Dwarven Grenadiers";
        case T::Journeyman: return "Journeymen";
        case T::Warrior: return "Warriors";
        case T::Berserker: return "Berserkers";
        case T::Catapult: return "Catapults";
        case T::Count: break;
    }
    return "?";
}

int warlordsStrength(UnitType type) {
    using T = UnitType;
    switch (type) {
        case T::LightInfantry: return 3;
        case T::HeavyInfantry: return 4;
        case T::Archers: return 4;
        case T::Pikemen: return 4;
        case T::LightCavalry: return 4;
        case T::HeavyCavalry: return 6;
        case T::Dwarves: return 5;
        case T::WolfRiders: return 5;
        case T::Giants: return 6;
        case T::Minotaurs: return 6;
        case T::Spiders: return 5;
        case T::Griffins: return 6;
        case T::Pegasi: return 6;
        case T::Elephants: return 7;
        case T::Unicorns: return 6;
        case T::Wizards: return 7;
        case T::Dragons: return 9;
        case T::Devils: return 8;
        case T::Demons: return 8;
        case T::Undead: return 5;
        case T::Archons: return 8;
        case T::Hero: return 4;  // level 1; every level adds one, to 9
        case T::DwarfGrenadier: return 4;
        case T::Journeyman: return 2;
        case T::Warrior: return 5;
        case T::Berserker: return 5;
        case T::Catapult: return 3;  // weak in the field; +2 against a town (Campaign::battle)
        case T::Count: break;
    }
    return 1;
}

}  // namespace illyria::sim
