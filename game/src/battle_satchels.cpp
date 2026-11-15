// The satchel trap as the player's play, and the precious few who throw a blast (the world's rules:
// engine/sim/src/world_satchels.cpp). Detonate and Hold fire for the selection, the marks over the charges his side
// has laid (a ring as wide as the blast, red with a friend inside), what a squad still carries (its card), the word and
// the line for a blast that told ("Ambush: 9 down") or was wasted on water ("Doused", "Steam"), and how the computer
// keeps its own dwarf and wizard: behind the foot, away from a foe that closes, mining the gate or the ford it holds.
// One path for a Skirmish and a Conquest, for the player and the computer: all of it gives the world's own commands.
#include <algorithm>
#include <cmath>

#include "illyria/game/battle.h"

namespace illyria::game {

namespace {

V3 dir2(const V3& from, const V3& to) {
    const V3 d{to.x - from.x, to.y - from.y, 0};
    const float len = d.length();
    return len > 1e-4f ? d * (1.0f / len) : V3{1, 0, 0};
}

// A blast's dead are counted together while they keep falling: the tally closes this long after the last.
constexpr uint32_t kTallyQuiet = sim::kTicksPerSecond * 3 / 2;
// This many of the foe down in one tally is worth the word.
constexpr int kAmbushMen = 3;
// The computer: a foe this near its dwarf or wizard and he is taken out of the way; a choke this near is his to mine.
constexpr float kThreatM = 14.0f;
constexpr float kChokeM = 90.0f;
// The charges of a mined place lie this far apart (each inside the next one's blast), and he waits this far behind them.
constexpr float kMineGapM = 4.5f;
constexpr float kMineWaitM = 13.0f;

bool laid(const sim::Projectile& p) { return p.kind == sim::ProjectileKind::Bomb && !p.thrown && p.fuse == 0 && p.resting; }

float flat(const V3& a, const V3& b) { return V3{a.x - b.x, a.y - b.y, 0}.length(); }

}  // namespace

bool Battle::selectionSparks() const {
    for (sim::EntityId id : selectedAlive())
        if (sim::World::sparks(world_->units()[id - 1].stats)) return true;
    return false;
}

bool Battle::selectionStores() const {
    for (sim::EntityId id : selectedAlive()) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.stats.ranged.ammo > 0 || u.stats.satchels.count > 0) return true;
    }
    return false;
}

bool Battle::selectionHoldsFire() const {
    bool any = false;
    for (sim::EntityId id : selectedAlive()) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.stats.ranged.muzzleSpeed <= sim::Fixed::zero() && u.stats.satchels.count == 0) continue;
        if (!u.holdFire) return false;
        any = true;
    }
    return any;
}

void Battle::holdFireSelected() {
    std::vector<sim::EntityId> shooters;
    for (sim::EntityId id : selectedAlive()) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.stats.ranged.muzzleSpeed > sim::Fixed::zero() || u.stats.satchels.count > 0) shooters.push_back(id);
    }
    if (shooters.empty()) {
        say("No shooters selected");
        return;
    }
    const bool hold = !selectionHoldsFire();
    for (sim::EntityId id : shooters) {
        sim::Command c{local_, id, sim::Command::Type::HoldFire, {}};
        c.hold = hold;
        pending_.push_back(c);
    }
    say(hold ? "Holding fire" : "Fire at will");
}

void Battle::orderDetonate(const V3& at, bool queue) {
    // The laid satchel nearest the tap (as the world will take it), else the ground there.
    V3 mark = at;
    float best = static_cast<float>(sim::World::kDetonatePick.toDouble());
    bool satchel = false;
    for (const sim::Projectile& p : world_->projectiles()) {
        if (!laid(p)) continue;
        const float d = flat(toV3(p.pos), at);
        if (d < best) best = d, mark = toV3(p.pos), satchel = true;
    }
    // Of those selected who can spark it, the nearest within his throw; with none in reach, the nearest, who walks up.
    sim::EntityId who = sim::kNoEntity;
    float nearest = 1e30f;
    bool reach = false, spent = false;
    for (sim::EntityId id : selectedAlive()) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!sim::World::sparks(u.stats)) continue;
        if (!sim::World::armed(u)) {
            spent = true;
            continue;
        }
        const float d = flat(toV3(u.pos), mark);
        const bool in = d <= static_cast<float>(u.stats.ranged.maxRange.toDouble());
        if ((in && !reach) || (in == reach && d < nearest)) who = id, nearest = d, reach = in;
    }
    if (who == sim::kNoEntity) {
        say(spent ? "Nothing left to throw" : "Select a dwarf or a wizard");
        return;
    }
    if (satchel && world_->douses(toSim(mark))) say("That satchel is wet");
    issue(local_, who, sim::Command::Type::Detonate, mark);
    pending_.back().queue = queue;
    markOrdered({who});
    voice_.order(Situation::Attack, *world_, {who});
}

Battle::Load Battle::squadLoad(const Squad& s) const {
    Load l;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        l.satchels += u.satchels;
        l.satchelsMax += u.stats.satchels.count;
        if (u.stats.ranged.ammo > 0) l.ammo += u.ammo, l.ammoMax += u.stats.ranged.ammo;
        l.holdFire = l.holdFire || u.holdFire;
    }
    return l;
}

std::string Battle::storeLeft() const {
    int satchels = 0, missiles = 0;
    bool lays = false, throws = false;
    sim::ProjectileKind kind = sim::ProjectileKind::Arrow;
    for (sim::EntityId id : selectedAlive()) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.stats.satchels.count > 0) lays = true, satchels += u.satchels;
        if (u.stats.ranged.ammo > 0) throws = true, missiles += u.ammo, kind = u.stats.ranged.kind;
    }
    if (!lays && !throws) return {};
    const auto count = [](int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
    const char* one = kind == sim::ProjectileKind::Molotov ? "Molotov" : kind == sim::ProjectileKind::Stone ? "stone" : "fireball";
    const char* many = kind == sim::ProjectileKind::Molotov ? "Molotovs" : kind == sim::ProjectileKind::Stone ? "stones" : "fireballs";
    if (lays && throws) return count(satchels, "satchel", "satchels") + ", " + count(missiles, one, many) + " left";
    if (lays) return satchels > 0 ? count(satchels, "satchel", "satchels") + " left" : "No satchels left";
    return missiles > 0 ? count(missiles, one, many) + " left" : std::string("No ") + many + " left";
}

std::vector<Battle::SatchelMark> Battle::satchelMarks() const {
    std::vector<SatchelMark> out;
    // Shown with a man who lays or sparks them selected, or while the next tap is a lay or a detonation.
    bool show = orderMode_ == OrderMode::LaySatchel || orderMode_ == OrderMode::Detonate;
    for (sim::EntityId id : selectedAlive()) {
        const sim::Unit& u = world_->units()[id - 1];
        show = show || u.stats.satchels.count > 0 || sim::World::sparks(u.stats);
    }
    if (!show) return out;
    for (const sim::Projectile& p : world_->projectiles()) {
        if (!laid(p) || p.owner != local_) continue;
        SatchelMark m;
        m.pos = toV3(p.pos);
        m.radius = static_cast<float>(p.explosive.radius.toDouble());
        m.dead = world_->douses(p.pos);
        for (const sim::Unit& u : world_->units()) {
            if (!u.alive() || u.inAir() || flat(toV3(u.pos), m.pos) >= m.radius) continue;
            (u.owner == local_ ? m.friends : m.foes) += 1;
        }
        out.push_back(m);
    }
    return out;
}

bool Battle::satchelShown(const sim::Projectile& p) const {
    if (p.kind != sim::ProjectileKind::Bomb || p.owner == local_ || !laid(p)) return true;
    // A foe's charge lying in wait: small, and only where the viewer's own side sees the ground (fog).
    const V3 at = toV3(p.pos);
    return sees(p.owner, at.x, at.y);
}

void Battle::collectBlasts() {
    const uint32_t now = world_->tick();
    // Water's word, where it happened.
    for (const sim::ImpactEvent& e : world_->impacts()) {
        if (e.kind != sim::ImpactEvent::Kind::Doused && e.kind != sim::ImpactEvent::Kind::Steam) continue;
        const V3 at = toV3(e.pos);
        if (!sees(local_, at.x, at.y) && conquest_) continue;
        floats_.push_back({now, sim::kNoEntity, at, e.kind == sim::ImpactEvent::Kind::Doused ? "Doused" : "Steam", false});
    }
    // The dead of a satchel's blast, of the foe (those the viewer may see) and of his own.
    for (const sim::DeathEvent& d : world_->deathEvents()) {
        if (d.cause != sim::DamageType::Explosive) continue;
        const V3 at = toV3(d.pos);
        const bool own = d.owner == local_;
        if (!own && !sees(d.owner, at.x, at.y)) continue;
        if (blast_.foes + blast_.own == 0) blast_.at = {0, 0, 0};
        blast_.at = blast_.at + at;
        (own ? blast_.own : blast_.foes) += 1;
        blast_.last = now;
    }
    if (blast_.foes + blast_.own == 0 || now - blast_.last < kTallyQuiet) return;
    const V3 at = blast_.at * (1.0f / static_cast<float>(blast_.foes + blast_.own));
    if (blast_.foes >= kAmbushMen) {
        const std::string word = "Ambush: " + std::to_string(blast_.foes) + " down";
        announce(word);
        floats_.push_back({now, sim::kNoEntity, at, word, true});
    }
    if (blast_.own > 0) {
        const std::string word = "Own blast: " + std::to_string(blast_.own) + " down";
        announce(word);
        floats_.push_back({now, sim::kNoEntity, at + V3{0, 0, 1.5f}, word, false});
    }
    blast_ = {};
}

// --- The computer's own -------------------------------------------------------------------------------------------

bool Battle::aiArmed(const Squad& s) const {
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.alive() && sim::World::armed(u)) return true;
    }
    return false;
}

bool Battle::aiChoke(const Squad& s, const V3& c, const V3& foe, V3& spot, V3& across, V3& wait) const {
    const sim::Region& rg = worldRegion();
    const auto vec = [](const V3& p) { return toSim(p); };
    // A gate it holds, still standing: the ground just inside the doors, where the storm must come through.
    float best = kChokeM;
    bool found = false;
    for (size_t i = 0; i < rg.gates().size(); ++i) {
        const sim::GateState& gs = world_->gates()[i];
        if (gs.holder != s.owner) continue;
        const V3 a = toV3(rg.gates()[i].a), b = toV3(rg.gates()[i].b), mid = (a + b) * 0.5f;
        const float d = flat(mid, c);
        if (d >= best || d < 4.0f) continue;
        const V3 in = V3{c.x - mid.x, c.y - mid.y, 0} * (1.0f / d);
        // Clear of the gatehouse: the first place inward where a line can lie down the way in (the column that breaks in
        // comes along it), each charge where a man can stand.
        for (float inward = 8.0f; inward <= 24.0f; inward += 2.0f) {
            const V3 at = mid + in * (inward + kMineGapM);  // (the middle of the three)
            if (!world_->standable(vec(at)) || !world_->standable(vec(at + in * kMineGapM)) || !world_->standable(vec(at - in * kMineGapM))) continue;
            best = d;
            found = true;
            spot = at;
            across = in;
            wait = spot + in * (kMineGapM + kMineWaitM);
            break;
        }
    }
    if (found) return true;
    // A ford between it and the foe: its own bank, clear of the water.
    const V3 ford = toV3(rg.ford());
    if (!world_->douses(rg.ford()) || flat(ford, c) > kChokeM) return false;
    const float span = flat(foe, c);
    if (span < 1.0f) return false;
    const V3 toFoe = V3{foe.x - c.x, foe.y - c.y, 0} * (1.0f / span);
    const float along = (ford.x - c.x) * toFoe.x + (ford.y - c.y) * toFoe.y;
    if (along < 8.0f || along > span) return false;  // not between them
    // Not once his own foot are over it: the ford is mined by a side that waits behind it, not one that has crossed.
    for (const Squad& f : squads_) {
        if (f.owner != s.owner || aliveCount(f) == 0 || roleOf(f.type) != Role::Melee) continue;
        const V3 fc = squadCentre(f, false);
        if ((fc.x - ford.x) * toFoe.x + (fc.y - ford.y) * toFoe.y > 0) return false;
    }
    const float len = flat(ford, c);
    const V3 home = V3{c.x - ford.x, c.y - ford.y, 0} * (1.0f / len);
    float out = 0;
    while (out < len && world_->douses(vec(ford + home * out))) out += 1.0f;
    if (out >= len - 4.0f) return false;
    spot = ford + home * (out + 4.0f);
    across = V3{-home.y, home.x, 0};
    wait = spot + home * kMineWaitM;
    return !world_->douses(vec(spot + across * kMineGapM)) && !world_->douses(vec(spot - across * kMineGapM));
}

bool Battle::aiPrecious(size_t squad, const V3& foe) {
    const Squad& s = squads_[squad];
    const Role role = roleOf(s.type);
    if (role != Role::Bomber && role != Role::Caster) return false;
    if (aiMemo_.size() < squads_.size()) aiMemo_.resize(squads_.size());
    AiMemo& memo = aiMemo_[squad];
    const V3 c = squadCentre(s, false);
    bool armed = false, laying = false;
    int satchels = 0;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        armed = armed || sim::World::armed(u);
        satchels += u.satchels;
        laying = laying || u.layOrdered || !u.queue.empty();
    }
    // Behind the nearest of its side's foot, on their far side from `from`.
    const auto behindFoot = [&](const V3& from) {
        const Squad* cover = nullptr;
        float best = 1e30f;
        for (const Squad& f : squads_) {
            if (f.owner != s.owner || &f == &s || aliveCount(f) == 0 || roleOf(f.type) != Role::Melee || f.type == sim::UnitType::Hero || aiShattered(f)) continue;
            const float d = flat(squadCentre(f, false), c);
            if (d < best) best = d, cover = &f;
        }
        const V3 away = dir2(from, c);
        return cover ? squadCentre(*cover, false) + away * 14.0f : c + away * 25.0f;
    };
    // A foe closing on him: out of the way, deaf to all else. He is never sent into a melee.
    const sim::EntityId threat = nearestFoe(s.owner, c, kThreatM);
    if (threat != sim::kNoEntity) {
        if (!s.garrison) aiForceMarch(squad, behindFoot(toV3(world_->units()[threat - 1].pos)));
        memo.mineAt = {};
        memo.mined = true;  // (no going back to a place the foe has reached)
        return true;
    }
    if (laying) return true;  // at his work: no other order
    // A place to mine, once: a gate of his side's, or the ford before the foe. A line of three, then back behind it to wait.
    if (!memo.mined && satchels >= 2 && armed) {
        V3 spot, across, wait;
        if (aiChoke(s, c, foe, spot, across, wait)) {
            memo.mined = true;
            std::vector<sim::EntityId> men;
            for (sim::EntityId id : s.members)
                if (world_->units()[id - 1].alive() && world_->units()[id - 1].satchels > 0) men.push_back(id);
            if (!men.empty()) {
                const sim::EntityId id = men.front();  // one dwarf lays the line
                const int n = std::min(3, world_->units()[id - 1].satchels);
                const auto queued = [&](sim::Command::Type type, const V3& to, bool queue) {
                    sim::Command cmd{s.owner, id, type, toSim(to)};
                    cmd.queue = queue;
                    // (As the player's deployment: what he mines at the battle's first look was laid beforehand.)
                    cmd.force = type == sim::Command::Type::LaySatchel && !conquest_ && world_->tick() < 2 * sim::kTicksPerSecond;
                    pending_.push_back(cmd);
                };
                for (int k = 0; k < n; ++k) queued(sim::Command::Type::LaySatchel, spot + across * (static_cast<float>(k - n / 2) * kMineGapM), k > 0);
                queued(sim::Command::Type::Move, wait, true);
                memo.mineAt = wait;
                memo.mining = true;
                return true;
            }
        }
    }
    // Waiting on his trap: he stays where he can throw at it (the world sparks it with foes on it and no friend).
    if (memo.mining) {
        bool lies = false;
        for (const sim::Projectile& p : world_->projectiles()) lies = lies || (laid(p) && p.owner == s.owner && flat(toV3(p.pos), memo.mineAt) < kMineWaitM + 12.0f);
        if (lies && armed) return true;
        memo.mining = false;
    }
    // Nothing left to throw or lay: behind the foot, and no nearer.
    if (!armed && satchels == 0) {
        if (!s.garrison) {
            const V3 to = behindFoot(foe);
            if (flat(to, c) > 8.0f) aiForceMarch(squad, to);
        }
        return true;
    }
    return false;
}

}  // namespace illyria::game
