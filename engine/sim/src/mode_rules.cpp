// The rules deciding a match (mode_rules.h): Horde's hill and waves, capture
// the flag's flags, and the price of a squad.
#include "illyria/sim/mode_rules.h"

#include <algorithm>
#include <array>

#include "illyria/sim/rng.h"

namespace illyria::sim {

namespace {

void hashVec(StateHasher& h, const Vec3& v) {
    h.addI64(v.x.raw);
    h.addI64(v.y.raw);
    h.addI64(v.z.raw);
}

bool within(const Unit& u, const Vec3& at, int32_t metres) {
    const Fixed r = Fixed::fromInt(metres);
    return (u.pos - at).flat().lengthSq() <= r * r;
}

}  // namespace

bool standsGround(const Unit& u) { return u.alive() && !u.routed && !u.inAir(); }

// --- The hill ----------------------------------------------------------------

void HillRule::place(const Vec3& at, PlayerId holders, int32_t radiusM) {
    *this = {};
    at_ = at;
    holders_ = holders;
    radiusM_ = radiusM;
}

void HillRule::count(const World& world) {
    holdersIn_ = foesIn_ = 0;
    for (const Unit& u : world.units()) {
        if (!standsGround(u) || !within(u, at_, radiusM_)) continue;
        (u.owner == holders_ ? holdersIn_ : foesIn_) += 1;
    }
}

void HillRule::step(const World& world) {
    if (lost_) return;
    count(world);
    if (holdersIn_ > 0) {
        ++held_;
        contested_ = 0;
    } else if (foesIn_ > 0 && ++contested_ >= kLoseTicks) {
        lost_ = true;
    }
}

void HillRule::hash(StateHasher& h) const {
    hashVec(h, at_);
    h.addU64(holders_);
    h.addI64(radiusM_);
    h.addI64(holdersIn_);
    h.addI64(foesIn_);
    h.addI64(contested_);
    h.addI64(held_);
    h.addU64(lost_);
}

// --- The flags ---------------------------------------------------------------

void FlagRule::add(const Vec3& at, PlayerId team) {
    Flag f;
    f.at = at;
    f.team = team;
    flags_.push_back(f);
}

std::vector<size_t> FlagRule::step(const World& world) {
    std::vector<size_t> fell;
    for (size_t i = 0; i < flags_.size(); ++i) {
        Flag& f = flags_[i];
        if (!f.standing) continue;
        f.defenders = f.attackers = f.threats = 0;
        std::array<int32_t, 256> on{};
        for (const Unit& u : world.units()) {
            if (!standsGround(u)) continue;
            if (u.owner == f.team) {
                f.defenders += within(u, f.at, kRadiusM);
                continue;
            }
            if (within(u, f.at, kThreatM)) ++f.threats;
            if (within(u, f.at, kRadiusM)) {
                ++f.attackers;
                ++on[u.owner];
            }
        }
        if (f.attackers == 0 || f.defenders > 0) continue;
        // Taken: by the team with the most men on it (the lowest on a tie).
        f.standing = false;
        f.fellTick = world.tick();
        f.takenBy = static_cast<PlayerId>(std::max_element(on.begin(), on.end()) - on.begin());
        fell.push_back(i);
    }
    return fell;
}

const FlagRule::Flag* FlagRule::flagOf(PlayerId team) const {
    for (const Flag& f : flags_)
        if (f.team == team) return &f;
    return nullptr;
}

int FlagRule::standing() const {
    int n = 0;
    for (const Flag& f : flags_) n += f.standing;
    return n;
}

PlayerId FlagRule::lastStanding() const {
    if (standing() != 1) return kNobody;
    for (const Flag& f : flags_)
        if (f.standing) return f.team;
    return kNobody;
}

void FlagRule::hash(StateHasher& h) const {
    h.addU64(flags_.size());
    for (const Flag& f : flags_) {
        hashVec(h, f.at);
        h.addU64(f.team);
        h.addU64(f.standing);
        h.addU64(f.takenBy);
        h.addU64(f.fellTick);
        h.addI64(f.defenders);
        h.addI64(f.attackers);
        h.addI64(f.threats);
    }
}

// --- The waves ---------------------------------------------------------------

int32_t waveThreat(UnitType type) {
    switch (type) {
        case UnitType::Undead:
        case UnitType::LightInfantry:
        case UnitType::Archers: return 2;
        case UnitType::Berserker:
        case UnitType::WolfRiders:
        case UnitType::LightCavalry:
        case UnitType::DwarfGrenadier: return 3;
        case UnitType::Spiders: return 4;
        case UnitType::Wizards: return 5;
        case UnitType::Minotaurs: return 8;
        case UnitType::Giants: return 10;
        case UnitType::Devils: return 12;
        case UnitType::Demons: return 15;
        case UnitType::Dragons: return 40;
        default: return 3;
    }
}

namespace {

// The horde's kinds, in the order they join it, and the wave each joins at.
struct Kin {
    UnitType type;
    int32_t from;  // first wave
    int32_t most;  // the most of them in one group
};
constexpr Kin kKin[] = {
    {UnitType::Undead, 1, 30},       {UnitType::LightInfantry, 2, 24}, {UnitType::Archers, 2, 14},
    {UnitType::WolfRiders, 3, 12},   {UnitType::Spiders, 3, 10},       {UnitType::Berserker, 4, 14},
    {UnitType::Minotaurs, 4, 5},     {UnitType::LightCavalry, 5, 10},  {UnitType::Giants, 6, 3},
    {UnitType::Devils, 7, 3},        {UnitType::DwarfGrenadier, 7, 6}, {UnitType::Wizards, 8, 3},
    {UnitType::Demons, 9, 2},
};
// Each wave's theme: what most of it is. The dead, men, beasts, brutes and
// fiends in turn (as they have joined).
constexpr UnitType kThemes[] = {UnitType::Undead, UnitType::LightInfantry, UnitType::WolfRiders, UnitType::Undead,
                                UnitType::Minotaurs, UnitType::Berserker, UnitType::Spiders, UnitType::Devils,
                                UnitType::LightCavalry, UnitType::Giants};

// The most of a kind in one group, larger for more seats on the hill.
int32_t mostOf(UnitType t, int32_t holders) {
    for (const Kin& k : kKin)
        if (k.type == t) return k.most * (1 + holders) / 2;
    return 4;
}

}  // namespace

WavePlan hordeWave(int32_t wave, int32_t difficulty, int32_t holders, uint64_t seed) {
    WavePlan plan;
    plan.wave = wave = std::max(1, wave);
    plan.boss = wave % 5 == 0;
    plan.elite = !plan.boss && wave % 3 == 0;
    Rng rng(seed ^ (static_cast<uint64_t>(wave) * 0x9E3779B97F4A7C15ULL), 0x40DE);
    // The threat it carries: growing by a half of the first wave each round,
    // more on harder settings and for every seat holding the hill.
    static constexpr int32_t kDifficultyPct[3] = {70, 100, 140};
    const int32_t pct = kDifficultyPct[std::clamp(difficulty, 0, 2)];
    holders = std::clamp(holders, 1, 4);
    int32_t budget = (24 + 12 * (wave - 1)) * pct / 100 * (100 + 60 * (holders - 1)) / 100;
    if (plan.boss) budget = budget * 13 / 10;
    const int32_t rank = std::min(3, (wave - 1) / 4 + (plan.elite ? 1 : 0));
    // From one edge at first, then from more at once.
    const int32_t edges = std::min(4, 1 + (wave - 1) / 2);
    const uint8_t firstEdge = static_cast<uint8_t>(rng.nextBelow(8));
    const auto edgeOf = [&](size_t g) { return static_cast<uint8_t>((firstEdge + (g % static_cast<size_t>(edges)) * 8 / edges) % 8); };

    if (plan.boss) {
        // The champion leads from the first edge, a level up for every five waves.
        WaveGroup b;
        b.type = wave % 10 == 0 ? UnitType::Dragons : UnitType::Demons;
        b.count = 1 + wave / 20;
        b.rank = std::min(3, wave / 5);
        b.edge = firstEdge;
        b.boss = true;
        plan.groups.push_back(b);
        budget -= std::min(budget / 2, b.count * waveThreat(b.type));
    }
    std::vector<UnitType> joined;
    for (const Kin& k : kKin)
        if (k.from <= wave) joined.push_back(k.type);
    const UnitType theme = [&] {
        const UnitType t = kThemes[(wave - 1) % (sizeof kThemes / sizeof kThemes[0])];
        for (UnitType j : joined)
            if (j == t) return t;
        return UnitType::Undead;
    }();
    // A theme group with half the threat, the rest in more kinds as it grows.
    const int32_t others = std::min<int32_t>(static_cast<int32_t>(joined.size()) - 1, 1 + wave / 3);
    std::vector<UnitType> kinds{theme};
    std::vector<UnitType> pool;
    for (UnitType j : joined)
        if (j != theme) pool.push_back(j);
    for (int32_t i = 0; i < others && !pool.empty(); ++i) {
        const size_t k = rng.nextBelow(static_cast<uint32_t>(pool.size()));
        kinds.push_back(pool[k]);
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(k));
    }
    int32_t left = budget;
    for (size_t g = 0; g < kinds.size(); ++g) {
        const UnitType t = kinds[g];
        const int32_t share = g == 0 ? budget / 2 : (budget - budget / 2) / static_cast<int32_t>(kinds.size() - 1);
        int32_t count = std::clamp(share / waveThreat(t), 1, mostOf(t, holders));
        count = std::min(count, std::max(1, left / waveThreat(t)));
        left -= count * waveThreat(t);
        plan.groups.push_back({t, count, rank, edgeOf(plan.groups.size()), false});
    }
    // What is left swells the groups that have room, the theme first.
    for (WaveGroup& g : plan.groups) {
        if (g.boss) continue;
        const int32_t more = std::min(mostOf(g.type, holders) - g.count, left / waveThreat(g.type));
        if (more <= 0) continue;
        g.count += more;
        left -= more * waveThreat(g.type);
    }
    for (const WaveGroup& g : plan.groups) plan.strength += g.count * waveThreat(g.type);
    return plan;
}

void hashWave(StateHasher& h, const WavePlan& plan) {
    h.addI64(plan.wave);
    h.addU64(plan.elite);
    h.addU64(plan.boss);
    h.addI64(plan.strength);
    h.addU64(plan.groups.size());
    for (const WaveGroup& g : plan.groups) {
        h.addU64(static_cast<uint64_t>(g.type));
        h.addI64(g.count);
        h.addI64(g.rank);
        h.addU64(g.edge);
        h.addU64(g.boss);
    }
}

// --- Picks -------------------------------------------------------------------

const std::vector<Offer>& squadOffers() {
    // Squads, as in Myth's unit trading: the line, the missiles, the
    // specialists, and the costly heavy troops.
    static const std::vector<Offer> kOffers = {
        {UnitType::Warrior, 6, 4},       {UnitType::HeavyInfantry, 6, 4}, {UnitType::Pikemen, 6, 3},
        {UnitType::Archers, 6, 4},       {UnitType::Dwarves, 5, 4},       {UnitType::Berserker, 5, 4},
        {UnitType::DwarfGrenadier, 3, 4}, {UnitType::Journeyman, 2, 3},   {UnitType::LightCavalry, 4, 4},
        {UnitType::HeavyCavalry, 4, 6},  {UnitType::Wizards, 2, 6},       {UnitType::Griffins, 2, 6},
    };
    return kOffers;
}

}  // namespace illyria::sim
