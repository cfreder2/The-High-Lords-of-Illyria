// Sorties (docs/GAME_DESIGN.md, "Outworks and timber"; the owner: "light cavalry sorties that can head out, and each has a
// Molotov plus their swords; this way you can't approach undefended"). A garrison's light horse ride out by a door that is
// safe (a capital's second door, its sally port, first) against an engine that is in reach of the walls and weakly guarded,
// throw their one bottle of oil at it (a Molotov: it burns an engine to nothing), cut down what stands by it, and ride back
// in. The garrison decides when (garrisonSorties, on its gate's beat), for a computer lord's and a neutral town's; the
// player orders one (Command::Sortie) and it is the same ride. An escorted train meets it (world_siege.cpp's escort): a
// sortie into an escort turns back. Derived from the men's state and the seeded dice only, Fixed throughout.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// Engines this near the castle are worth a sortie; riders this many at most go; it is worth it when they are this many
// times the power of the guard within kGuardReach of the engine.
constexpr Fixed kSortieRange = Fixed::fromInt(230), kGuardReach = Fixed::fromInt(30);
constexpr size_t kSortieRiders = 6;
constexpr int64_t kSortieOdds = 2;
// A door is safe with no foe this near its outside.
constexpr Fixed kDoorSafe = Fixed::fromInt(25);
// A sortie turns back after this long, with the rider under half his strength, or with foes about him more than this many
// to one of his fellows near him.
constexpr uint32_t kSortieTicks = 60 * kTicksPerSecond;
constexpr Fixed kDangerReach = Fixed::fromInt(12), kFellowReach = Fixed::fromInt(20);
constexpr int32_t kDangerOdds = 2;
// Out of the door this far before the ride at the engine.
constexpr Fixed kOutside = Fixed::fromInt(14);

bool isEngine(const Unit& u) { return u.stats.gait == Gait::Wheeled && u.stats.ranged.muzzleSpeed > Fixed::zero(); }
bool rider(const Unit& u) { return u.stats.mounted && u.stats.melee.damage > 0 && u.stats.mass <= Fixed::fromInt(3); }
Vec3 doorMid(const Gate& g) { return (g.a + g.b) * Fixed::fromRatio(1, 2); }

}  // namespace

bool World::sortieWorth(const Unit& who, const Unit& mark) const {
    // The guard by the engine, against the riders of his side about him (his own sortie's strength).
    int64_t guard = 0, riders = 0;
    nearby(mark.pos.x, mark.pos.y, kGuardReach, [&](uint32_t k) {
        const Unit& o = units_[k];
        if (!o.alive() || o.routed || o.owner != mark.owner || isEngine(o) || (o.pos - mark.pos).flat().length() > kGuardReach) return;
        guard += fightingPower(o);
    });
    size_t n = 0;
    nearby(who.pos.x, who.pos.y, kFellowReach, [&](uint32_t k) {
        const Unit& o = units_[k];
        if (n >= kSortieRiders || !o.alive() || o.owner != who.owner || !rider(o)) return;
        riders += fightingPower(o);
        ++n;
    });
    return n >= 2 && riders >= guard * kSortieOdds;
}

void World::garrisonSorties(const std::vector<size_t>& doors) {
    if (!config_.region || doors.empty() || (tick_ / 10) % 3 != 0) return;
    const Region& rg = *config_.region;
    const GateState& lead = gates_[doors.front()];
    if (!lead.recalls) return;  // (the player's horse ride when he says)
    const PlayerId holder = lead.holder;
    const Rampart& ring = rg.ramparts()[static_cast<size_t>(rg.gates()[doors.front()].ring)];
    // The riders at hand: idle horse of the garrison inside the walls; none while a sortie is out.
    std::vector<Unit*> horse;
    bool out = false;
    nearby(ring.centre.x, ring.centre.y, kSortieRange, [&](uint32_t k) {
        Unit& u = units_[k];
        if (!u.alive() || u.owner != holder || !rider(u)) return;
        if (u.sortie != Unit::Sortie::None) out = true;
        if (u.routed || !Gate::inside(ring, u.pos) || u.targetUnit != kNoEntity || u.foe != kNoEntity || (u.hasMoveTarget && !u.stationed) || !u.queue.empty()) return;
        horse.push_back(&u);
    });
    if (out || horse.size() < 2) return;
    // The engine: in reach of the walls, outside them, weakly guarded, the nearest.
    const Unit* mark = nullptr;
    Fixed best;
    nearbyNotOf(holder, ring.centre.x, ring.centre.y, kSortieRange, [&](uint32_t k) {
        const Unit& e = units_[k];
        if (!e.alive() || e.owner == holder || !isEngine(e) || e.burning > 0 || Gate::inside(ring, e.pos)) return;
        const Fixed d = (e.pos - ring.centre).flat().length();
        if (d > kSortieRange || (mark != nullptr && d >= best) || !sortieWorth(*horse.front(), e)) return;
        mark = &e;
        best = d;
    });
    if (mark == nullptr) return;
    // A door safe to go out by: no foe at its outside, no barricade in its way; the sally port first, else the nearest.
    size_t door = rg.gates().size();
    for (const bool side : {true, false})
        for (const size_t d : doors) {
            if (door < rg.gates().size()) break;
            const Gate& g = rg.gates()[d];
            if (g.side != side) continue;
            const Vec3 outside = doorMid(g) + g.out * kOutside;
            bool foe = false;
            nearbyNotOf(holder, outside.x, outside.y, kDoorSafe, [&](uint32_t k) {
                const Unit& f = units_[k];
                foe = foe || (f.alive() && f.owner != holder && !f.routed && (f.pos - outside).flat().length() < kDoorSafe);
            });
            bool barred = false;
            for (const WorkState& w : works_) barred = barred || (w.gate == static_cast<int32_t>(d) && w.barring && w.standing());
            if (!foe && !barred) door = d;
        }
    if (door == rg.gates().size()) return;
    const Gate& g = rg.gates()[door];
    std::sort(horse.begin(), horse.end(), [&](const Unit* a, const Unit* b) {
        const Fixed da = (a->pos - doorMid(g)).flat().lengthSq(), db = (b->pos - doorMid(g)).flat().lengthSq();
        return da < db || (da == db && a->id < b->id);
    });
    for (size_t i = 0; i < horse.size() && i < kSortieRiders; ++i) {
        Unit& u = *horse[i];
        u.sortie = Unit::Sortie::Out;
        u.sortieMark = mark->id;
        u.sortieHome = u.pos;
        u.sortieTick = tick_;
        Vec3 at = doorMid(g) + g.out * kOutside;
        at.z = groundHeight(at.x, at.y);
        u.routeFrom = u.pos;
        beginMove(u, at, Fixed::zero(), g.out);
        planPath(u, at);
        u.stationed = true;
    }
}

void World::startSortie(Unit& u, const Command& cmd) {
    if (!rider(u)) return;
    const Unit* mark = findUnit(cmd.targetUnit);
    if (mark == nullptr || !mark->alive() || mark->owner == u.owner) {
        // The engine nearest the place shown.
        mark = nullptr;
        Fixed best;
        nearbyNotOf(u.owner, cmd.target.x, cmd.target.y, Fixed::fromInt(30), [&](uint32_t k) {
            const Unit& e = units_[k];
            if (!e.alive() || !isEngine(e)) return;
            const Fixed d = (e.pos - cmd.target).flat().length();
            if (d > Fixed::fromInt(30) || (mark != nullptr && d >= best)) return;
            mark = &e;
            best = d;
        });
    }
    if (mark == nullptr) return;
    u.sortie = Unit::Sortie::Out;
    u.sortieMark = mark->id;
    u.sortieHome = u.pos;
    u.sortieTick = tick_;
}

bool World::sortieStep(Unit& u) {
    // The ride's phases are orders the rider is given as he goes (the walk and the fight are everyone's); this only turns
    // them. False: his tick goes on as any man's.
    const auto back = [&] {
        u.sortie = Unit::Sortie::Back;
        Command c{u.owner, u.id, Command::Type::Move, u.sortieHome};
        applyOrder(u, c);
        u.routeFrom = u.pos;
        beginMove(u, u.sortieHome, Fixed::zero(), {});
        planPath(u, u.sortieHome);
        u.stationed = true;
    };
    if (u.sortie == Unit::Sortie::Back) {
        if (!u.hasMoveTarget || (u.pos - u.sortieHome).flat().length() < Fixed::fromInt(4)) {
            u.sortie = Unit::Sortie::None;
            u.sortieMark = kNoEntity;
            u.hasMoveTarget = false;
        }
        return false;
    }
    const Unit* mark = findUnit(u.sortieMark);
    // Back once it is down, or alight and his bottle thrown (the fire does the rest: he does not stand in it), or after a
    // while, or hurt.
    if (mark == nullptr || !mark->alive() || (mark->burning > 0 && !armed(u)) || tick_ - u.sortieTick > kSortieTicks || u.hp * 2 < u.stats.maxHp) {
        back();
        return false;
    }
    // Too many about him: back, before he is caught.
    int32_t foes = 0, fellows = 0;
    nearby(u.pos.x, u.pos.y, kFellowReach, [&](uint32_t k) {
        const Unit& o = units_[k];
        if (!o.alive() || o.routed) return;
        const Fixed d = (o.pos - u.pos).flat().length();
        if (o.owner != u.owner && !isEngine(o) && d < kDangerReach) ++foes;
        if (o.owner == u.owner && o.sortie != Unit::Sortie::None && o.sortieMark == u.sortieMark && d < kFellowReach) ++fellows;
    });
    if (foes > fellows * kDangerOdds) {
        back();
        return false;
    }
    // Out of the door, then at the engine: the bottle from a stride or two off (his pursuit keeps a throw's reach while he
    // has one), then the sword.
    if (u.sortie == Unit::Sortie::Out && (!u.hasMoveTarget || u.targetUnit != kNoEntity)) {
        u.sortie = Unit::Sortie::Strike;
        Command c{u.owner, u.id, Command::Type::AttackUnit, mark->pos};
        c.targetUnit = mark->id;
        applyOrder(u, c);
        u.stationed = true;
    } else if (u.sortie == Unit::Sortie::Strike && u.targetUnit != mark->id) {
        Command c{u.owner, u.id, Command::Type::AttackUnit, mark->pos};
        c.targetUnit = mark->id;
        applyOrder(u, c);
        u.stationed = true;
    }
    return false;
}

}  // namespace illyria::sim
