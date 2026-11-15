// The capture point of a siege (battle_capture.h): each tick, count the attackers standing at the pole and the defenders
// who still hold its ground (alive, on their feet, not routed; the ground is the inside of the ring of walls the pole stands
// in, off the walks and outside any ring within it). With attackers at the pole and no defender on the ground the flag
// climbs kBase plus a point for every four of them (eight men at most) a tick; with defenders on the ground it stops; with
// no attacker at the pole it sinks a point a tick. At kFull the pole is taken, for good: the next pole is fought for (a
// capital's keep), and at the last the town is taken and the battle is over.
#include "illyria/game/battle_capture.h"

#include "illyria/game/battle.h"

#include <algorithm>
#include <string>

namespace illyria::game {

namespace {
// A ring of walls that lies wholly within another (a capital's inner bailey in its outer wall).
bool ringWithin(const sim::Rampart& inner, const sim::Rampart& outer) {
    return &inner != &outer && !inner.corners.empty() &&
           std::all_of(inner.corners.begin(), inner.corners.end(), [&](const sim::Vec3& c) { return sim::Gate::inside(outer, c); });
}
}  // namespace

void CapturePoint::place(const sim::Vec3& at, sim::PlayerId defender, sim::PlayerId attacker, int ring) {
    *this = {};
    active_ = true;
    stages_ = {Stage{at, ring}};
    defender_ = defender;
    attacker_ = attacker;
}

void CapturePoint::placeNeutral(const sim::Vec3& at, sim::PlayerId a, sim::PlayerId b) {
    place(at, b, a);
    neutral_ = true;
}

void CapturePoint::stepNeutral(const sim::World& world) {
    const sim::Fixed r = sim::Fixed::fromInt(kNeutralRadiusM);
    const sim::Vec3 pole = at();
    int32_t mine = 0, theirs = 0;  // of the host whose flag is on the pole, and of the other
    for (const sim::Unit& u : world.units()) {
        if (!u.alive() || u.routed || u.inAir() || (u.owner != attacker_ && u.owner != defender_)) continue;
        if ((u.pos - pole).flat().lengthSq() <= r * r) ++(u.owner == attacker_ ? mine : theirs);
    }
    if (theirs > 0 && mine == 0) {
        // The other host alone at the pole: this flag comes down, and from the foot of the pole theirs goes up.
        progress_ = std::max(0, progress_ - kHaulDown);
        if (progress_ == 0) {
            std::swap(attacker_, defender_);
            std::swap(mine, theirs);
        }
    } else if (mine > 0 && theirs == 0) {
        // kFull in kNeutralSeconds: five points every nine ticks.
        static_assert(kFull * 9 == kNeutralSeconds * sim::kTicksPerSecond * 5);
        if (world.tick() % 9 < 5) ++progress_;
    }
    attackersIn_ = mine;
    defendersIn_ = theirs;
    if (progress_ >= kFull) taken_ = true;
}

void CapturePoint::addStage(const sim::Vec3& at, int ring) { stages_.push_back({at, ring}); }

bool CapturePoint::onGround(const sim::World& world, const sim::Vec3& p) const {
    const Stage& s = stages_[stage_];
    const sim::Region* rg = world.region();
    if (s.ring == kNoRing || rg == nullptr || static_cast<size_t>(s.ring) >= rg->ramparts().size()) {
        const sim::Fixed r = sim::Fixed::fromInt(kRadiusM);
        return (p - s.at).flat().lengthSq() <= r * r;
    }
    const std::vector<sim::Rampart>& rings = rg->ramparts();
    const sim::Rampart& mine = rings[static_cast<size_t>(s.ring)];
    if (!sim::Gate::inside(mine, p) || rg->rampartAt(p.x, p.y, p.z) >= 0) return false;
    // Not in a ring within this one (a capital's inner bailey is the keep's ground, not the courtyard's).
    for (const sim::Rampart& inner : rings)
        if (ringWithin(inner, mine) && sim::Gate::inside(inner, p)) return false;
    return true;
}

void CapturePoint::step(const sim::World& world) {
    if (!active_ || taken_) return;
    if (neutral_) return stepNeutral(world);
    const sim::Fixed r = sim::Fixed::fromInt(kRadiusM);
    const sim::Vec3 pole = at();
    int32_t att = 0, def = 0;
    for (const sim::Unit& u : world.units()) {
        if (!u.alive() || u.routed || u.inAir() || (u.owner != attacker_ && u.owner != defender_) || !onGround(world, u.pos)) continue;
        if (u.owner == defender_) ++def;
        else if ((u.pos - pole).flat().lengthSq() <= r * r) ++att;
    }
    attackersIn_ = att;
    defendersIn_ = def;
    if (att == 0) progress_ -= kBase;
    else if (def == 0) progress_ += kBase + std::min(att, kMostMen) / kMenPerPoint;
    progress_ = std::clamp(progress_, 0, kFull);
    if (progress_ < kFull) return;
    if (stage_ + 1 < stages_.size()) {
        // This pole is theirs for good; the next is fought for.
        ++stage_;
        progress_ = 0;
        attackersIn_ = defendersIn_ = 0;
    } else {
        taken_ = true;
    }
}

void Battle::placeCapturePoint(const sim::Continent::City& town, sim::PlayerId keeper) {
    // In the courtyard before the keep: the town's defenders hold it at the start (their garrison stands there), the other
    // side comes to take it. Its ground is the inside of the town's outer ring; a ring within that (a capital's inner bailey)
    // is the keep's, with a pole of its own behind its doors.
    const sim::Region& rg = *region_;
    const sim::Vec3 at = town.castle.courtyard - rg.origin();
    const size_t outer = world_->gateOfTown(town.pos - rg.origin());
    const int ring = outer == sim::World::kNoGate ? CapturePoint::kNoRing : rg.gates()[outer].ring;
    capture_.place({at.x, at.y, sim::Fixed::zero()}, keeper, keeper == kPlayer ? kEnemy : kPlayer, ring);
    if (ring == CapturePoint::kNoRing) return;
    const sim::Rampart& walls = rg.ramparts()[static_cast<size_t>(ring)];
    for (const sim::Gate& door : rg.gates()) {
        const sim::Rampart& inner = rg.ramparts()[static_cast<size_t>(door.ring)];
        if (!ringWithin(inner, walls)) continue;
        // Behind the bailey's doors, on the first open ground.
        const sim::Vec3 mid = (door.a + door.b) * sim::Fixed::fromRatio(1, 2);
        sim::Vec3 pole = mid - door.out * sim::Fixed::fromInt(8);
        for (int back = 6; back <= 16; back += 2) {
            const sim::Vec3 p = mid - door.out * sim::Fixed::fromInt(back);
            if (!world_->standable(p) || !sim::Gate::inside(inner, p)) continue;
            pole = p;
            break;
        }
        capture_.addStage({pole.x, pole.y, sim::Fixed::zero()}, door.ring);
        break;
    }
}

std::vector<Battle::SiegeGate> Battle::siegeGates() const {
    std::vector<SiegeGate> out;
    if (!capture_.active()) return out;
    const auto& gates = worldRegion().gates();
    const int outer = capture_.stages().front().ring;
    int doors = 0;
    for (const sim::Gate& g : gates) doors += g.ring == outer;
    for (const CapturePoint::Stage& stage : capture_.stages())
        for (size_t i = 0; i < gates.size(); ++i) {
            if (gates[i].ring != stage.ring) continue;
            const char* name = stage.ring != outer ? "Keep gate" : doors < 2 ? "Gate" : gates[i].side ? "Side gate" : "Road gate";
            out.push_back({name, world_->gates()[i].hp * 100 / std::max(1, gates[i].maxHp)});
        }
    return out;
}

std::string Battle::siegeLine() const {
    if (!capture_.active()) return "";
    if (capture_.neutral()) {
        const bool ours = capture_.attacker() == local_;
        const std::string flag = (ours ? "Your flag " : "Their flag ") + std::to_string(capture_.progress() * 100 / CapturePoint::kFull) + "%";
        if (capture_.taken()) return ours ? "Your flag flies over the isle" : "Their flag flies over the isle";
        if (capture_.blocked()) return capture_.progress() > 0 ? flag + ", contested" : "The isle is contested";
        if (capture_.attackersIn() > 0) return flag + ", rising";
        if (capture_.defendersIn() > 0 && capture_.progress() > 0) return flag + ", coming down";
        return capture_.progress() > 0 ? flag : "Raise your flag on the isle";
    }
    const bool mine = capture_.attacker() == local_, keep = capture_.stage() > 0, castle = capture_.stages().size() > 1;
    const std::string pole = keep ? "keep" : "courtyard", Pole = keep ? "Keep" : "Courtyard";
    const std::string pct = " " + std::to_string(capture_.progress() * 100 / CapturePoint::kFull) + "%";
    if (capture_.taken()) return mine ? "The town is yours" : "The town is lost";
    if (mine) {
        if (capture_.blocked()) return "Defenders hold the " + (keep ? std::string("keep") : castle ? std::string("bailey") : pole);
        if (capture_.attackersIn() > 0) return Pole + pct;
        if (capture_.progress() > 0) return Pole + pct + ", falling";
        return keep ? "Courtyard taken. Take the keep" : "Take the courtyard";
    }
    if (capture_.attackersIn() > 0 && capture_.defendersIn() == 0) return Pole + " falling" + pct;
    if (capture_.attackersIn() > 0) return "Your men hold the " + pole;
    return keep ? "Courtyard lost. Hold the keep" : "Hold the courtyard";
}

std::string Battle::siegeResult() const {
    if (brokeOff_) return "The besiegers break off.";
    if (!capture_.taken()) return "";
    if (capture_.neutral()) return capture_.attacker() == local_ ? "Your flag flies over the isle." : "Their flag flies over the isle.";
    if (capture_.attacker() != local_) return "The town is lost.";
    return capture_.stages().size() > 1 ? "The keep is taken." : "The courtyard is taken.";
}

void Battle::testStorm() {
    if (!capture_.active()) return;
    for (size_t g = 0; g < world_->gates().size(); ++g) world_->setGateHp(g, 0);
    const V3 at = toV3(capture_.at());
    for (const Squad& s : squads_)
        if (s.owner == kPlayer && aliveCount(s) > 0 && roleOf(s.type) != Role::Siege) moveSquad(s, at);
}

}  // namespace illyria::game
