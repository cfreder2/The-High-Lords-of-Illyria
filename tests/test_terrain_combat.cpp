#include <memory>

#include "illyria/sim/scenario.h"
#include "illyria/sim/terrain.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};

std::shared_ptr<const Terrain> hills() {
    return std::make_shared<const Terrain>(Terrain::generate(7, 64, m(4), m(30)));
}
}  // namespace

TEST(terrain_is_deterministic_and_bounded) {
    const Terrain a = Terrain::generate(7, 64, m(4), m(30));
    const Terrain b = Terrain::generate(7, 64, m(4), m(30));
    const Terrain c = Terrain::generate(8, 64, m(4), m(30));
    CHECK(a.samples() == b.samples());
    CHECK(a.samples() != c.samples());
    Fixed lo = m(1000), hi = m(-1000);
    for (Fixed h : a.samples()) {
        lo = min(lo, h);
        hi = max(hi, h);
    }
    CHECK(lo >= Fixed::zero());
    CHECK(hi <= m(30));
    CHECK(hi - lo > m(3));  // actually hilly
}

TEST(terrain_interpolates_between_samples) {
    const Terrain t = Terrain::generate(7, 64, m(4), m(30));
    CHECK(t.heightAt(m(8), m(12)) == t.sample(2, 3));
    // Midway along a cell edge.
    CHECK(t.heightAt(m(10), m(12)) == (t.sample(2, 3) + t.sample(3, 3)) / 2);
    // The cell centre lies on the a-d diagonal shared by both triangles.
    CHECK(t.heightAt(m(10), m(14)) == (t.sample(2, 3) + t.sample(3, 4)) / 2);
    // Outside the region clamps to the edge.
    CHECK(t.heightAt(m(-50), m(-50)) == t.sample(0, 0));
}

TEST(units_stand_on_terrain_and_arrows_land_on_it) {
    WorldConfig cfg;
    cfg.terrain = hills();
    World w(cfg);
    const EntityId archer = w.spawnUnit(0, {m(40), m(128), Fixed::zero()}, kEast, scenarios::archer());
    CHECK(w.findUnit(archer)->pos.z == cfg.terrain->heightAt(m(40), m(128)));
    const Command cmd{0, archer, Command::Type::AttackGround, {m(100), m(128), Fixed::zero()}};
    int grounded = 0;
    for (int i = 0; i < 300; ++i) {
        w.step(i == 0 ? std::span(&cmd, 1) : std::span<const Command>{});
        for (const ImpactEvent& e : w.impacts()) {
            ++grounded;
            CHECK_NEAR(e.pos.z.toDouble(), cfg.terrain->heightAt(e.pos.x, e.pos.y).toDouble(), 0.25);
        }
    }
    CHECK(grounded > 0);
}

TEST(arrows_go_over_a_ridge_between_bow_and_mark) {
    // Flat ground but for a ridge 5 m high at x = 48..56, between an archer at
    // x = 30 and the spot he is sent to shoot at, x = 80, out of sight beyond it.
    // The flat shot would bury in the ridge; lofted just over it, his arrows
    // come down at the spot.
    constexpr int kCells = 32;
    std::vector<Fixed> samples((kCells + 1) * (kCells + 1), Fixed::zero());
    for (int iy = 0; iy <= kCells; ++iy)
        for (int ix = 12; ix <= 14; ++ix) samples[static_cast<size_t>(iy * (kCells + 1) + ix)] = m(5);
    WorldConfig cfg;
    cfg.terrain = std::make_shared<const Terrain>(Terrain::fromSamples(kCells, m(4), samples));
    World w(cfg);
    const EntityId archer = w.spawnUnit(0, {m(30), m(64), Fixed::zero()}, kEast, scenarios::archer());
    const Command cmd{0, archer, Command::Type::AttackGround, {m(80), m(64), Fixed::zero()}};
    int landed = 0, onRidge = 0;
    for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
        w.step(i == 0 ? std::span(&cmd, 1) : std::span<const Command>{});
        for (const ImpactEvent& e : w.impacts()) {
            if (e.pos.x < m(60)) ++onRidge;
            else ++landed;
            CHECK_NEAR(e.pos.x.toDouble(), 80.0, 8.0);
        }
    }
    CHECK(landed >= 4);
    CHECK(onRidge == 0);
    // The ridge stands in the flat shot's way, and the scan finds the arc that clears it.
    const Vec3 bow{m(30), m(64), m(1) + Fixed::fromRatio(44, 100)}, spot{m(80), m(64), Fixed::zero()};
    const Fixed need = w.clearingTan(bow, spot, m(2));
    CHECK(need > Fixed::fromRatio(15, 100) && need < Fixed::one());
}

TEST(footmen_charge_and_kill_an_archer_in_melee) {
    World w(WorldConfig{});
    const EntityId archer = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, scenarios::archer());
    const EntityId f1 = w.spawnUnit(1, {m(15), m(0), m(0)}, -kEast, scenarios::footman());
    const EntityId f2 = w.spawnUnit(1, {m(15), m(3), m(0)}, -kEast, scenarios::footman());
    int meleeBlows = 0;
    for (int i = 0; i < 30 * kTicksPerSecond && w.findUnit(archer)->alive(); ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) meleeBlows += e.projectile == kNoEntity;
    }
    CHECK(!w.findUnit(archer)->alive());
    CHECK(meleeBlows > 0);
    CHECK(w.findUnit(f1)->alive() || w.findUnit(f2)->alive());
}

TEST(idle_archers_shoot_approaching_enemies) {
    World w(WorldConfig{});
    for (int i = 0; i < 10; ++i) w.spawnUnit(0, {m(0), m(i - 5), m(0)}, kEast, scenarios::archer());
    // Outside the footman's aggro range, inside the archers' auto-fire range.
    const EntityId f = w.spawnUnit(1, {m(30), m(0), m(0)}, -kEast, scenarios::footman());
    int shots = 0;
    // Three volleys: drawn on first (1.5 s), then one a reload (4 s), each 0.7 s in the air.
    for (int i = 0; i < 12 * kTicksPerSecond; ++i) {
        w.step({});
        shots += static_cast<int>(w.impacts().size());
    }
    CHECK(shots > 0);
    CHECK(w.findUnit(f)->hp < scenarios::footman().maxHp);
}
