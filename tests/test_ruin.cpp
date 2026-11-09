// A ruin in battle (Region::fromContinent, its stones, floor and stair from
// the model, tools/models/ruin.py): it stands on level ground, men climb onto
// its platform by the stair and no other way, fight up there, and never stand
// inside its columns, walls or pit.
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

#include "illyria/sim/continent.h"
#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
const Continent::Site& ruin(const char* name) {
    for (const Continent::Site& s : continent().sites())
        if (s.name == name) return s;
    return continent().sites().front();
}
std::shared_ptr<const Region> window(const Continent::Site& s) { return std::make_shared<const Region>(Region::fromContinent(continent(), s.pos, 256)); }
std::shared_ptr<const Region> hallsWindow() {
    static const auto r = window(ruin("Sunken Halls"));
    return r;
}
Vec3 local(const Region& r, const Vec3& p) { return {p.x - r.origin().x, p.y - r.origin().y, p.z}; }

// The floor of the ruin at this site (the one whose deck is nearest it).
const Floor& floorOf(const Region& r, const Continent::Site& s) {
    const Vec3 at = local(r, s.pos);
    const Floor* best = &r.floors().front();
    for (const Floor& f : r.floors())
        if ((r.decks()[f.deck].centre - at).flat().length() < (r.decks()[best->deck].centre - at).flat().length()) best = &f;
    return *best;
}

// Inside one of the site's stones by more than a hand's breadth.
bool insideStone(const Region& r, const Vec3& p) {
    const Fixed slack = Fixed::fromRatio(1, 10);
    for (const Obstacle& o : r.obstacles()) {
        if (o.variant != Obstacle::kSiteStone) continue;
        if (o.kind == Obstacle::Kind::House ? abs(p.x - o.pos.x) < o.halfX - slack && abs(p.y - o.pos.y) < o.halfY - slack
                                            : (p - o.pos).flat().length() < o.radius - slack)
            return true;
    }
    return false;
}

// Places on the floor a squad can be sent to: on its nav grid with half a
// metre clear all round, a pace and a half apart (the floor as drawn is cramped).
std::vector<Vec3> spotsOn(const Region& r, const Floor& f, const NavGrid& nav, size_t count) {
    const Deck& d = r.decks()[f.deck];
    std::vector<Vec3> out;
    for (int64_t j = -7; j <= 7 && out.size() < count; ++j)
        for (int64_t i = -9; i <= 9 && out.size() < count; ++i) {
            const Vec3 p{d.centre.x + d.halfSpan * i / 10, d.centre.y + d.halfWidth * j / 8, d.centre.z};
            bool clear = nav.floorAt(p, false) >= 0;
            for (int k = 0; k < 8 && clear; ++k) {
                const Fixed a = kTwoPi * k / 8;
                clear = nav.clearLine(p, p + Vec3{cos(a) / 2, sin(a) / 2, Fixed::zero()});
            }
            for (const Vec3& q : out) clear = clear && (q - p).flat().length() >= Fixed::fromRatio(3, 2);
            if (clear) out.push_back(p);
        }
    return out;
}
}  // namespace

TEST(ruins_stand_on_level_ground) {
    // Every ruin's footprint (its corners at about two thirds of its size from
    // the middle) is level with the site, so its platform neither floats nor sinks.
    const Fixed reach = m(Continent::Site::kRuinSize) * 7 / 10;
    for (const Continent::Site& s : continent().sites()) {
        if (s.kind != Continent::Site::Kind::Ruin) continue;
        Fixed worst;
        for (int k = 0; k < 16; ++k) {
            const Fixed a = kTwoPi * k / 16;
            for (const Fixed d : {reach / 2, reach}) {
                const Fixed z = continent().heightAt(s.pos.x + cos(a) * d, s.pos.y + sin(a) * d);
                worst = max(worst, abs(z - s.pos.z));
            }
        }
        if (worst > Fixed::fromRatio(1, 20)) std::printf("  %s: ground off level by %.2f m\n", s.name.c_str(), worst.toDouble());
        CHECK(worst <= Fixed::fromRatio(1, 20));
    }
    // And so is the battle's terrain under it.
    const Region& r = *hallsWindow();
    const Continent::Site& s = ruin("Sunken Halls");
    const Vec3 at = local(r, s.pos);
    for (int k = 0; k < 16; ++k) {
        const Fixed a = kTwoPi * k / 16;
        CHECK(abs(r.terrain().heightAt(at.x + cos(a) * reach, at.y + sin(a) * reach) - s.pos.z) <= Fixed::fromRatio(1, 10));  // (the window's 16 m landform samples interpolate: a centimetre or two off the pad)
    }
}

TEST(ruin_floor_is_open_only_by_its_stair) {
    const Region& r = *hallsWindow();
    const Continent::Site& s = ruin("Sunken Halls");
    const Floor& f = floorOf(r, s);
    const Deck& floor = r.decks()[f.deck];
    const Deck& stair = r.decks()[f.stair];
    const NavGrid nav(r);
    // The platform stands a man's height and a half above the ground; the
    // stair climbs to it from the ground before it.
    CHECK(floor.centre.z - s.pos.z > m(2) && floor.centre.z - s.pos.z < m(4));
    const Vec3 lowEnd = stair.centre - stair.dir * stair.halfSpan, highEnd = stair.centre + stair.dir * stair.halfSpan;
    CHECK(abs(stair.heightAt(lowEnd.x, lowEnd.y) - s.pos.z) < Fixed::fromRatio(1, 10));
    CHECK(abs(stair.heightAt(highEnd.x, highEnd.y) - floor.centre.z) < Fixed::fromRatio(1, 10));
    CHECK(!stair.contains(f.foot.x, f.foot.y) && !floor.contains(f.foot.x, f.foot.y));
    const std::vector<Vec3> spots = spotsOn(r, f, nav, 6);
    CHECK(spots.size() >= 4);
    for (const Vec3& p : spots) {
        CHECK(nav.floorAt(p, false) >= 0);
        CHECK(nav.blocked(p));  // solid, seen from the ground
        // From behind the platform, not straight up: round by the stair.
        const Vec3 away = (floor.centre - f.foot).flat() / (floor.centre - f.foot).flat().length();
        const Vec3 side = p + away * (floor.halfSpan + floor.halfWidth + m(2));
        CHECK(!nav.clearLine(side, p));
        std::vector<Vec3> route;
        CHECK(nav.findPath(side, p, route));
        bool viaFoot = false;
        for (const Vec3& w : route) viaFoot = viaFoot || (w - f.foot).flat().length() < Fixed::one();
        CHECK(viaFoot);
    }
    CHECK(nav.clearLine(f.foot, f.bottom));
}

TEST(men_climb_onto_the_ruin_by_its_stair_and_stand_clear_of_its_stones) {
    const auto r = hallsWindow();
    const Continent::Site& s = ruin("Sunken Halls");
    const Floor& f = floorOf(*r, s);
    const Deck& floor = r->decks()[f.deck];
    const Deck& stair = r->decks()[f.stair];
    const NavGrid nav(*r);
    WorldConfig cfg;
    cfg.region = r;
    World w(cfg);
    // A dozen men behind the platform, on the side away from its stair, each sent to a spot on its floor.
    const Vec3 away = (floor.centre - f.foot).flat() / (floor.centre - f.foot).flat().length();
    const Vec3 start = floor.centre + away * (max(floor.halfSpan, floor.halfWidth) + m(20));
    const std::vector<Vec3> spots = spotsOn(*r, f, nav, 12);
    CHECK(spots.size() >= 8);
    std::vector<EntityId> men;
    std::vector<Command> orders;
    for (size_t i = 0; i < spots.size(); ++i) {
        const Vec3 at = start + Vec3{m(static_cast<int64_t>(i % 4) * 2), m(static_cast<int64_t>(i / 4) * 2), Fixed::zero()};
        men.push_back(w.spawnUnit(0, at, -away, unitStats(i % 3 == 2 ? UnitType::Archers : UnitType::LightInfantry)));
        orders.push_back({0, men.back(), Command::Type::Move, spots[i]});
    }
    w.step(std::span(orders.data(), orders.size()));
    std::vector<Fixed> lastZ(men.size());
    for (size_t i = 0; i < men.size(); ++i) lastZ[i] = w.findUnit(men[i])->pos.z;
    int climbedElsewhere = 0, inStone = 0;
    for (int t = 0; t < 120 * kTicksPerSecond; ++t) {
        w.step({});
        for (size_t i = 0; i < men.size(); ++i) {
            const Unit& u = *w.findUnit(men[i]);
            // Up off the ground (more than a step): only on the stair.
            if (lastZ[i] <= s.pos.z + Fixed::one() && u.pos.z > s.pos.z + Fixed::one() && !stair.contains(u.pos.x, u.pos.y)) ++climbedElsewhere;
            lastZ[i] = u.pos.z;
            if (insideStone(*r, u.pos)) ++inStone;
        }
    }
    int up = 0;
    for (size_t i = 0; i < men.size(); ++i) {
        const Unit& u = *w.findUnit(men[i]);
        const bool there = (u.pos - spots[i]).flat().length() < m(2) && abs(u.pos.z - floor.centre.z) < Fixed::fromRatio(1, 5);
        if (!there) {
            std::printf("  man %zu at (%.1f, %.1f, %.1f), sent to (%.1f, %.1f); path", i, u.pos.x.toDouble(), u.pos.y.toDouble(), u.pos.z.toDouble(),
                        spots[i].x.toDouble(), spots[i].y.toDouble());
            for (size_t k = 0; k < u.path.size(); ++k) std::printf("%s(%.1f, %.1f)", k == u.pathIndex ? " >" : " ", u.path[k].x.toDouble(), u.path[k].y.toDouble());
            std::printf("\n");
        }
        up += there ? 1 : 0;
    }
    std::printf("  %d of %zu men up on the platform\n", up, men.size());
    CHECK(up == static_cast<int>(men.size()));
    CHECK(climbedElsewhere == 0);
    CHECK(inStone == 0);
    // And back down to the ground beyond, the same way.
    std::vector<Command> back;
    for (size_t i = 0; i < men.size(); ++i) back.push_back({0, men[i], Command::Type::Move, start + Vec3{m(static_cast<int64_t>(i) * 2), Fixed::zero(), Fixed::zero()}});
    w.step(std::span(back.data(), back.size()));
    for (int t = 0; t < 120 * kTicksPerSecond; ++t) w.step({});
    int down = 0;
    for (size_t i = 0; i < men.size(); ++i) {
        const Unit& u = *w.findUnit(men[i]);
        const bool there = (u.pos - back[i].target).flat().length() < m(2) && u.pos.z < r->terrain().heightAt(u.pos.x, u.pos.y) + Fixed::fromRatio(1, 5);
        if (!there) {
            std::printf("  man %zu at (%.1f, %.1f, %.1f), sent to (%.1f, %.1f); path", i, u.pos.x.toDouble(), u.pos.y.toDouble(), u.pos.z.toDouble(),
                        back[i].target.x.toDouble(), back[i].target.y.toDouble());
            for (size_t k = 0; k < u.path.size(); ++k) std::printf("%s(%.1f, %.1f)", k == u.pathIndex ? " >" : " ", u.path[k].x.toDouble(), u.path[k].y.toDouble());
            std::printf("\n");
        }
        down += there ? 1 : 0;
    }
    std::printf("  %d of %zu men back down\n", down, men.size());
    CHECK(down == static_cast<int>(men.size()));
}

TEST(men_fight_on_the_ruin_floor) {
    const auto r = hallsWindow();
    const Continent::Site& s = ruin("Sunken Halls");
    const Floor& f = floorOf(*r, s);
    const Deck& floor = r->decks()[f.deck];
    const NavGrid nav(*r);
    WorldConfig cfg;
    cfg.region = r;
    World w(cfg);
    // Six of ours go up onto the platform, then six of theirs, each to their
    // own spots; then theirs set on ours.
    const std::vector<Vec3> spots = spotsOn(*r, f, nav, 12);
    CHECK(spots.size() >= 8);
    const size_t half = spots.size() / 2;
    std::vector<EntityId> ours, theirs;
    std::vector<Command> orders;
    const Vec3 toward = (f.foot - floor.centre).flat() / (f.foot - floor.centre).flat().length();
    const Vec3 across{-toward.y, toward.x, Fixed::zero()};
    for (size_t i = 0; i < half; ++i) {
        ours.push_back(w.spawnUnit(0, f.foot + toward * m(6) + across * (m(static_cast<int64_t>(i) * 2) - m(5)), -toward, unitStats(UnitType::HeavyInfantry)));
        orders.push_back({0, ours.back(), Command::Type::Move, spots[i]});
    }
    w.step(std::span(orders.data(), orders.size()));
    for (int t = 0; t < 60 * kTicksPerSecond; ++t) w.step({});
    orders.clear();
    for (size_t i = 0; i < half; ++i) {
        theirs.push_back(w.spawnUnit(1, f.foot + toward * m(6) + across * (m(static_cast<int64_t>(i) * 2) - m(5)), -toward, unitStats(UnitType::HeavyInfantry)));
        Command c;
        c.player = 1;
        c.unit = theirs.back();
        c.type = Command::Type::Move;
        c.target = spots[half + i];
        c.force = true;  // up past ours without stopping to fight
        orders.push_back(c);
    }
    w.step(std::span(orders.data(), orders.size()));
    // Blows struck up on the floor (the one struck standing on it), and below it.
    int blowsUp = 0, blowsBelow = 0;
    std::vector<EntityId> all = ours;
    all.insert(all.end(), theirs.begin(), theirs.end());
    std::vector<int32_t> hp;
    for (EntityId id : all) hp.push_back(w.findUnit(id)->hp);
    const auto watch = [&] {
        for (size_t i = 0; i < all.size(); ++i) {
            const Unit& u = *w.findUnit(all[i]);
            if (u.hp < hp[i]) ++(u.pos.z > floor.centre.z - Fixed::one() ? blowsUp : blowsBelow);
            hp[i] = u.hp;
        }
    };
    for (int t = 0; t < 60 * kTicksPerSecond; ++t) {
        w.step({});
        watch();
    }
    int upThere = 0;
    for (EntityId id : all) upThere += w.findUnit(id)->alive() && w.findUnit(id)->pos.z > floor.centre.z - Fixed::fromRatio(1, 2) ? 1 : 0;
    std::printf("  %d of %zu men up on the platform a minute on\n", upThere, all.size());
    orders.clear();
    for (size_t i = 0; i < theirs.size(); ++i) {
        Command c;
        c.player = 1;
        c.unit = theirs[i];
        c.type = Command::Type::AttackUnit;
        c.targetUnit = ours[i % ours.size()];
        orders.push_back(c);
    }
    w.step(std::span(orders.data(), orders.size()));
    for (int t = 0; t < 90 * kTicksPerSecond; ++t) {
        w.step({});
        watch();
    }
    std::printf("  %d blows below\n", blowsBelow);
    int fallen = 0;
    for (EntityId id : ours) fallen += w.findUnit(id)->alive() ? 0 : 1;
    for (EntityId id : theirs) fallen += w.findUnit(id)->alive() ? 0 : 1;
    std::printf("  %d blows landed up on the platform, %d men fell\n", blowsUp, fallen);
    CHECK(blowsUp > 0);
    CHECK(fallen > 0);
}
