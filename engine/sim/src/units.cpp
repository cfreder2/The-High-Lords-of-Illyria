#include "illyria/sim/units.h"

namespace illyria::sim {

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

void bow(UnitStats& s, Fixed speed, int32_t reloadTicks, int32_t damage, Fixed dispersion, Fixed autoRange) {
    s.ranged.muzzleSpeed = speed;
    s.ranged.reloadTicks = reloadTicks;
    s.ranged.damage = damage;
    s.ranged.dispersionAt100m = dispersion;
    s.ranged.autoRange = autoRange;
    s.ranged.kind = ProjectileKind::Arrow;
}

void explosive(UnitStats& s, ProjectileKind kind, Fixed speed, int32_t reloadTicks, Fixed dispersion,
               Fixed autoRange, Fixed radius, int32_t damage, Fixed impulse, int32_t fuseTicks = 0) {
    s.ranged.muzzleSpeed = speed;
    s.ranged.reloadTicks = reloadTicks;
    s.ranged.dispersionAt100m = dispersion;
    s.ranged.autoRange = autoRange;
    s.ranged.kind = kind;
    s.ranged.explosive = {radius, damage, impulse, fuseTicks, r(7, 20)};
}

void shield(UnitStats& s, Fixed blockChance) { s.shield.blockChance = blockChance; }

}  // namespace

UnitStats unitStats(UnitType type) {
    using T = UnitType;
    UnitStats s;
    switch (type) {
        case T::LightInfantry:
            s = base(type, r(2, 5), r(9, 5), m(2), 55);
            melee(s, 12, r(3, 5), sec(6, 5));
            shield(s, r(1, 4));
            break;
        case T::HeavyInfantry:
            s = base(type, r(9, 20), r(19, 10), r(8, 5), 85);
            melee(s, 16, r(3, 5), sec(6, 5));
            shield(s, r(11, 20));
            s.mass = r(13, 10);
            break;
        case T::Archers:  // Warlords' Elven Archers
            s = base(type, r(2, 5), r(9, 5), r(11, 5), 40);
            bow(s, m(55), sec(2), 18, m(4), m(120));
            melee(s, 6, r(1, 2), sec(1));
            s.aggroRange = m(6);
            break;
        case T::Pikemen:
            s = base(type, r(2, 5), r(9, 5), r(9, 5), 60);
            melee(s, 15, r(11, 5), sec(4, 3));
            s.melee.bonusVsMountedPct = 150;
            s.terrain.openPct = 20;  // Warlords II: +1 in the open
            break;
        case T::LightCavalry:
            s = base(type, r(9, 10), r(13, 5), m(6), 80);
            melee(s, 14, r(4, 5), sec(1), m(3));
            s.mounted = true;
            s.mass = m(3);
            s.aggroRange = m(30);
            s.terrain.openPct = 20;  // +1 in the open
            break;
        case T::HeavyCavalry:
            s = base(type, r(19, 20), r(27, 10), r(9, 2), 120);
            melee(s, 22, r(4, 5), sec(6, 5), m(6));
            shield(s, r(2, 5));
            s.mounted = true;
            s.mass = m(4);
            s.aggroRange = m(30);
            s.terrain.openPct = 40;  // +2 in the open
            break;
        case T::Dwarves:  // Warlords' Dwarven Legions: axes and shields
            s = base(type, r(9, 20), r(27, 20), r(3, 2), 85);
            melee(s, 18, r(1, 2), sec(6, 5));
            shield(s, r(3, 10));
            s.mass = r(7, 5);
            s.terrain.hillsPct = 25;  // +1 in the hills
            break;
        case T::WolfRiders:
            s = base(type, r(4, 5), r(19, 10), r(13, 2), 70);
            melee(s, 14, r(3, 5), sec(1), m(2));
            s.mounted = true;
            s.mass = m(2);
            s.aggroRange = m(30);
            break;
        case T::Giants:
            s = base(type, r(6, 5), r(11, 2), r(11, 5), 320);
            melee(s, 45, r(3, 2), sec(2), m(12));
            s.mass = m(8);
            s.aggroRange = m(25);
            break;
        case T::Minotaurs:
            s = base(type, r(9, 10), r(16, 5), r(14, 5), 220);
            melee(s, 34, r(1, 1), sec(3, 2), m(8));
            s.mass = m(4);
            break;
        case T::Spiders:
            s = base(type, r(11, 10), r(7, 5), r(9, 2), 110);
            melee(s, 20, r(3, 5), sec(1));
            s.mass = m(2);
            s.aggroRange = m(25);
            break;
        case T::Griffins:
            s = base(type, r(6, 5), r(11, 5), m(7), 140);
            melee(s, 24, r(4, 5), sec(1), m(4));
            s.flyAltitude = m(12);
            s.mass = m(3);
            s.aggroRange = m(35);
            break;
        case T::Pegasi:  // winged horses with riders
            s = base(type, r(11, 10), r(23, 10), m(8), 90);
            melee(s, 14, r(4, 5), sec(1));
            s.flyAltitude = m(14);
            s.mounted = true;
            s.mass = r(5, 2);
            s.aggroRange = m(35);
            break;
        case T::Elephants:
            s = base(type, r(9, 5), m(4), m(3), 420);
            melee(s, 40, r(6, 5), sec(2), m(14));
            s.mounted = true;
            s.mass = m(12);
            break;
        case T::Unicorns:
            s = base(type, r(9, 10), r(11, 5), m(7), 110);
            melee(s, 26, r(1, 1), sec(6, 5), m(3));
            s.mass = m(3);
            s.aggroRange = m(30);
            break;
        case T::Wizards:
            s = base(type, r(2, 5), r(19, 10), m(2), 45);
            explosive(s, ProjectileKind::Fireball, m(30), sec(5), m(3), m(70), m(4), 45, m(9));
            melee(s, 4, r(1, 2), sec(1));
            s.aggroRange = m(5);
            break;
        case T::Dragons:
            s = base(type, r(5, 2), r(9, 2), m(7), 600);
            explosive(s, ProjectileKind::Fireball, m(35), sec(3), m(3), m(80), m(5), 50, m(10));
            melee(s, 40, r(6, 5), sec(3, 2), m(10));
            s.flyAltitude = m(18);
            s.mass = m(15);
            s.aggroRange = m(8);
            break;
        case T::Devils:
            s = base(type, r(1, 1), m(3), r(16, 5), 260);
            melee(s, 38, r(1, 1), sec(4, 3), m(6));
            s.mass = m(4);
            s.aggroRange = m(25);
            break;
        case T::Demons:
            s = base(type, r(11, 10), r(18, 5), r(7, 2), 300);
            melee(s, 42, r(6, 5), sec(4, 3), m(8));
            s.mass = m(5);
            s.aggroRange = m(25);
            break;
        case T::Undead:
            s = base(type, r(2, 5), r(9, 5), r(7, 5), 70);
            melee(s, 12, r(3, 5), sec(6, 5));
            s.bleeds = false;
            s.aggroRange = m(30);
            break;
        case T::Archons:  // winged angels
            s = base(type, r(4, 5), r(12, 5), m(6), 160);
            melee(s, 28, r(4, 5), sec(1), m(3));
            s.flyAltitude = m(10);
            s.mass = m(2);
            s.aggroRange = m(35);
            break;
        case T::Hero:
            s = base(type, r(1, 2), m(2), r(13, 5), 250);
            melee(s, 30, r(4, 5), sec(1), m(3));
            shield(s, r(11, 20));
            s.mass = m(2);
            s.aggroRange = m(25);
            break;
        case T::DwarfGrenadier:  // Myth's dwarves: satchel charges that bounce, roll and explode
            s = base(type, r(9, 20), r(13, 10), r(17, 10), 45);
            explosive(s, ProjectileKind::Bomb, m(16), sec(7, 2), m(10), m(24), r(9, 2), 60, m(11), sec(2));
            melee(s, 5, r(1, 2), sec(1));
            s.aggroRange = m(4);
            break;
        case T::Journeyman:  // Myth's healer
            s = base(type, r(2, 5), r(9, 5), r(11, 5), 50);
            s.healing = {35, r(3, 2), sec(2), 6};
            melee(s, 6, r(1, 2), sec(1));
            s.aggroRange = m(5);
            break;
        case T::Warrior:  // Myth's sword-and-shield infantry
            s = base(type, r(9, 20), r(19, 10), m(2), 75);
            melee(s, 18, r(3, 5), sec(11, 10));
            shield(s, r(13, 20));
            s.mass = r(6, 5);
            break;
        case T::Berserker:  // Myth's berserks: fast, hard-hitting, no shield
            s = base(type, r(9, 20), r(19, 10), r(17, 5), 80);
            melee(s, 26, r(3, 5), sec(14, 15));
            s.mass = r(6, 5);
            s.aggroRange = m(30);
            break;
        case T::Count:
            break;
    }
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
        case T::Count: break;
    }
    return "?";
}

}  // namespace illyria::sim
