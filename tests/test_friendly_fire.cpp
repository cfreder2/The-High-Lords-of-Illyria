// Friendly fire (docs/GAME_DESIGN.md, "Friendly fire"): a bowman's flat shot takes whoever stands in the line; the
// ones who throw a blast (stone, fireball, Molotov, satchel) aim so as to spare their own side, hold a volley for a
// friend in the way up to a cap, and then let it go (engine/sim/src/world_aoe.cpp).
#include <cstdio>
#include <vector>

#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
Fixed r(int64_t n, int64_t d) { return Fixed::fromRatio(n, d); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;

// A man who stands where he is put and does nothing.
UnitStats stander(UnitType type, int32_t hp = 0) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged = {};
    s.melee.damage = 0;
    s.shield.blockChance = Fixed::zero();
    s.satchels = {};
    if (hp > 0) s.maxHp = hp;
    return s;
}

struct Scene {
    World w;
    std::vector<EntityId> foes, friends;
    explicit Scene(uint64_t seed) : w(config(seed)) {}
    static WorldConfig config(uint64_t seed) {
        WorldConfig c;
        c.seed = seed;
        return c;
    }
    int32_t hpOf(const std::vector<EntityId>& ids) const {
        int32_t sum = 0;
        for (const EntityId id : ids) sum += std::max(0, w.findUnit(id)->hp);
        return sum;
    }
    int dead(const std::vector<EntityId>& ids) const {
        int n = 0;
        for (const EntityId id : ids) n += !w.findUnit(id)->alive();
        return n;
    }
};

// A catapult 100 m west of a pile of six foes round the origin, ordered to shell the middle of it; `n` of the
// catapult's own men stand about it, the nearest 2.5 m west of the middle.
Scene pile(uint64_t seed, int n, UnitType shooter = UnitType::Catapult, int64_t away = 100) {
    Scene s(seed);
    const EntityId cat = s.w.spawnUnit(0, {m(-away), m(0), m(0)}, kEast, unitStats(shooter));
    for (int k = 0; k < 6; ++k) {
        const Fixed x = r((k % 3 - 1) * 3, 2), y = r((k / 3) * 3 - 3 + 1, 2);
        s.foes.push_back(s.w.spawnUnit(1, {x, y, m(0)}, kWest, stander(UnitType::LightInfantry, 600)));
    }
    for (int k = 0; k < n; ++k) {
        const Fixed x = r(-9, 5) - m(k / 3), y = r((k % 3 - 1) * 8, 5);
        s.friends.push_back(s.w.spawnUnit(0, {x, y, m(0)}, kEast, stander(UnitType::LightInfantry, 60)));
    }
    const Command shell{0, cat, Command::Type::AttackGround, {m(0), m(0), m(0)}};
    s.w.step(std::span(&shell, 1));
    return s;
}

// Over `seeds` worlds of `pile(seed, n)`, 90 s of shelling: the friends' hit points lost, the foes', and the friends dead.
constexpr uint64_t kAloneSum = 0xcd43c73604019bef;  // the twelve worlds with no friend near, as on master before the rule

struct Toll {
    int32_t friendsLost = 0, foesLost = 0;
    int friendsDead = 0;
    uint64_t sum = 0;
};
Toll shell(int n, UnitType shooter = UnitType::Catapult, int64_t away = 100, int seeds = 12) {
    Toll t;
    for (int seed = 1; seed <= seeds; ++seed) {
        Scene s = pile(static_cast<uint64_t>(seed), n, shooter, away);
        const int32_t friendsBefore = s.hpOf(s.friends), foesBefore = s.hpOf(s.foes);
        for (int i = 0; i < 90 * kTicksPerSecond; ++i) s.w.step({});
        t.friendsLost += friendsBefore - s.hpOf(s.friends);
        t.foesLost += foesBefore - s.hpOf(s.foes);
        t.friendsDead += s.dead(s.friends);
        t.sum = t.sum * 31 + s.w.checksum();
    }
    return t;
}

}  // namespace

TEST(a_catapult_aims_to_spare_the_friends_beside_its_foes) {
    // Twelve worlds, six foes in a pile (600 hp each, so the pile lasts), a catapult 100 m off shelling the middle of it for
    // 90 s, and 0, 1 or 3 of its own men (60 hp) standing about 1.8 m from the middle. Measured on this scene before the rule
    // (the stone fell on the mark whoever stood by it): one friend, 90 hp lost and 1 dead; three friends, 824 hp lost and 4
    // dead; the foes lost 6156 hp with no friend and 6179 and 6328 with them there.
    const Toll alone = shell(0), one = shell(1), three = shell(3);
    std::printf("  no friends: foes lost %d (checksum %016llx) | one friend: friends lost %d (dead %d), foes %d | three: friends lost %d (dead %d), foes %d\n",
                alone.foesLost, static_cast<unsigned long long>(alone.sum), one.friendsLost, one.friendsDead, one.foesLost, three.friendsLost,
                three.friendsDead, three.foesLost);
    // No friend near: the catapult fires exactly as before (the same stones, the same worlds, tick for tick).
    CHECK(alone.foesLost == 6156);
    CHECK(alone.sum == kAloneSum);
    // Friends near: nearly none of them hurt (a tenth of the old toll at most), and the pile still shelled to near as much.
    CHECK(one.friendsLost * 10 <= 90 && one.friendsDead == 0);
    CHECK(three.friendsLost * 10 <= 824 && three.friendsDead == 0);
    CHECK(three.foesLost * 10 >= alone.foesLost * 7);  // (a wait, now and then, for the melee to shift: not a stalled engine)
    CHECK(one.foesLost * 10 >= alone.foesLost * 8);
}

TEST(an_archer_shoots_the_friend_standing_in_the_line) {
    // Six metres in front of the bowman, on the line to his foe: he does not look; the friend takes the arrows in the back.
    int hitFriend = 0, hitFoe = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Scene s(seed);
        const EntityId bow = s.w.spawnUnit(0, {m(-40), m(0), m(0)}, kEast, unitStats(UnitType::Archers));
        s.friends.push_back(s.w.spawnUnit(0, {m(-34), m(0), m(0)}, kEast, stander(UnitType::HeavyInfantry, 5000)));
        s.foes.push_back(s.w.spawnUnit(1, {m(0), m(0), m(0)}, kWest, stander(UnitType::HeavyInfantry, 5000)));
        const Command shoot{0, bow, Command::Type::AttackGround, {m(0), m(0), m(0)}};
        s.w.step(std::span(&shoot, 1));
        for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
            s.w.step({});
            for (const ImpactEvent& e : s.w.impacts()) {
                if (e.kind != ImpactEvent::Kind::Hit) continue;
                hitFriend += e.hitUnit == s.friends[0];
                hitFoe += e.hitUnit == s.foes[0];
            }
        }
    }
    std::printf("  arrows into the friend %d, into the foe %d\n", hitFriend, hitFoe);
    CHECK(hitFriend > 0);
}

// First tick (from the order) a stone smashes, in a pile ringed by friends if `ring`, whom a Move order sends off at tick `leave` if > 0.
int firstSmash(bool ring, int leave = 0) {
    Scene s(3);
    const EntityId cat = s.w.spawnUnit(0, {m(-100), m(0), m(0)}, kEast, unitStats(UnitType::Catapult));
    s.foes.push_back(s.w.spawnUnit(1, {m(0), m(0), m(0)}, kWest, stander(UnitType::LightInfantry, 5000)));
    if (ring)
        for (int k = 0; k < 8; ++k) {
            const Fixed a = r(k * 6283, 8 * 1000);
            s.friends.push_back(s.w.spawnUnit(0, {cos(a) * r(3, 2), sin(a) * r(3, 2), m(0)}, kEast, stander(UnitType::LightInfantry)));
        }
    const Command shell{0, cat, Command::Type::AttackGround, {m(0), m(0), m(0)}};
    s.w.step(std::span(&shell, 1));
    for (int i = 1; i < 40 * kTicksPerSecond; ++i) {
        if (i == leave)
            for (const EntityId f : s.friends) {
                const Vec3 at = s.w.findUnit(f)->pos;
                const Command go{0, f, Command::Type::Move, at + (at - Vec3{}).flat() * m(8)};
                s.w.step(std::span(&go, 1));
            }
        else
            s.w.step({});
        for (const ImpactEvent& e : s.w.impacts())
            if (e.kind == ImpactEvent::Kind::Smash) return i;
    }
    return -1;
}

TEST(a_catapult_holds_for_a_friend_in_the_way_and_looses_at_the_cap) {
    const int free = firstSmash(false), ringed = firstSmash(true), cleared = firstSmash(true, 15);
    std::printf("  first stone: free %.2f s, friends ringing the foe %.2f s, the friends sent off at 0.5 s %.2f s\n",
                free / 30.0, ringed / 30.0, cleared / 30.0);
    CHECK(free > 0);
    // Held the whole 5 s (a few ticks over for the retry), then loosed: a lord in desperation, not a stalemate.
    CHECK(ringed >= free + 4 * kTicksPerSecond && ringed <= free + 6 * kTicksPerSecond);
    // The friends gone from the blast: he looses as soon as they are clear, well before the cap.
    CHECK(cleared > free && cleared < ringed - 2 * kTicksPerSecond);
}

TEST(the_same_scene_twice_is_the_same_world) {
    CHECK(shell(3, UnitType::Catapult, 100, 2).sum == shell(3, UnitType::Catapult, 100, 2).sum);
    CHECK(shell(3, UnitType::Wizards, 40, 2).sum == shell(3, UnitType::Wizards, 40, 2).sum);
}

// A satchel lying 11 m from a grenadier, a foe 3.5 m beyond it (in its blast), and if `friend` one of his own men 3 m
// short of it (in it too): the tick of the first blast, or -1.
int firstBlast(bool friendNear, bool footOnHim = false) {
    Scene s(5);
    UnitStats dwarf = unitStats(UnitType::DwarfGrenadier);
    dwarf.satchels = {};
    dwarf.melee.damage = 0;
    dwarf.aggroRange = Fixed::zero();
    s.w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, dwarf);
    s.w.placeSatchel(0, {m(11), m(0), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge);
    s.foes.push_back(s.w.spawnUnit(1, {r(29, 2), m(0), m(0)}, kWest, stander(UnitType::HeavyInfantry, 5000)));
    if (friendNear) s.friends.push_back(s.w.spawnUnit(0, {m(8), m(0), m(0)}, kEast, stander(UnitType::HeavyInfantry, 5000)));
    (void)footOnHim;
    for (int i = 0; i < 30 * kTicksPerSecond; ++i) {
        s.w.step({});
        for (const ImpactEvent& e : s.w.impacts())
            if (e.kind == ImpactEvent::Kind::Explosion) return i;
    }
    return -1;
}

TEST(the_spark_waits_while_a_friend_is_in_the_satchels_blast_up_to_a_cap) {
    const int free = firstBlast(false), held = firstBlast(true);
    std::printf("  the spark: no friend %.2f s, a friend in the blast %.2f s\n", free / 30.0, held / 30.0);
    CHECK(free >= 0);
    CHECK(held >= free + 4 * kTicksPerSecond && held <= free + 7 * kTicksPerSecond);  // 5 s, then the one satchel with fewest friends
}

TEST(a_grenadier_lays_his_satchel_clear_of_his_own_men) {
    // Eight of his own crowd his feet; the foe is coming from 25 m. Every satchel he lays lies outside their reach.
    Scene s(2);
    const Explosive charge = unitStats(UnitType::DwarfGrenadier).satchels.charge;
    s.w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
    for (int k = 0; k < 8; ++k) {
        const Fixed a = r(k * 6283, 8 * 1000);
        s.friends.push_back(s.w.spawnUnit(0, {cos(a) * m(2), sin(a) * m(2), m(0)}, kEast, stander(UnitType::HeavyInfantry, 5000)));
    }
    s.foes.push_back(s.w.spawnUnit(1, {m(24), m(0), m(0)}, kWest, stander(UnitType::HeavyInfantry, 5000)));
    int laid = 0;
    bool clear = true;
    for (int i = 0; i < 6 * kTicksPerSecond; ++i) {
        s.w.step({});
        for (const Projectile& p : s.w.projectiles()) {
            if (p.kind != ProjectileKind::Bomb || p.fuse != 0) continue;
            ++laid;
            for (const EntityId f : s.friends) clear = clear && (s.w.findUnit(f)->pos - p.pos).flat().length() >= charge.radius;
        }
    }
    std::printf("  satchel-ticks seen %d, all clear of his men: %d\n", laid, clear ? 1 : 0);
    CHECK(laid > 0);
    CHECK(clear);
}

TEST(a_wizard_throws_his_fireball_so_as_to_spare_his_own_men) {
    // The same pile, a wizard 40 m off. Measured before the rule: three friends lost 903 hp in all (and the foes 2640, against
    // 2446 with no friend near).
    const Toll alone = shell(0, UnitType::Wizards, 40), three = shell(3, UnitType::Wizards, 40);
    std::printf("  wizard: no friends: foes lost %d | three friends: friends lost %d (dead %d), foes %d\n", alone.foesLost, three.friendsLost,
                three.friendsDead, three.foesLost);
    CHECK(three.friendsLost * 5 <= 903);                // a fifth of the old toll at most
    CHECK(three.foesLost * 10 >= alone.foesLost * 9);   // and the pile as hard hit
}
