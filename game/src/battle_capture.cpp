// The capture point of a siege (battle_capture.h): each tick, count the men of
// each side standing within the radius (alive, on their feet, not routed);
// the side with more raises its flag. The attackers' climb is kBase plus one
// point a man they are ahead by (eight at most) a tick; when the defenders
// are ahead it comes down as fast; even numbers hold it where it is, and an
// empty courtyard lets it sink a point a tick. At kFull the town is taken,
// for good: the battle is over.
#include "illyria/game/battle_capture.h"

#include "illyria/game/battle.h"

#include <algorithm>

namespace illyria::game {

void CapturePoint::place(const sim::Vec3& at, sim::PlayerId defender, sim::PlayerId attacker) {
    *this = {};
    active_ = true;
    at_ = at;
    defender_ = defender;
    attacker_ = attacker;
}

void CapturePoint::step(const sim::World& world) {
    if (!active_ || taken_) return;
    const sim::Fixed r = sim::Fixed::fromInt(kRadiusM);
    int32_t att = 0, def = 0;
    for (const sim::Unit& u : world.units()) {
        if (!u.alive() || u.routed || u.inAir()) continue;
        if ((u.pos - at_).flat().lengthSq() > r * r) continue;
        if (u.owner == attacker_) ++att;
        else if (u.owner == defender_) ++def;
    }
    attackersIn_ = att;
    defendersIn_ = def;
    if (att > def) progress_ += kBase + std::min(att - def, kMostAhead);
    else if (def > att) progress_ -= kBase + std::min(def - att, kMostAhead);
    else if (att == 0) progress_ -= 1;
    progress_ = std::clamp(progress_, 0, kFull);
    if (progress_ >= kFull) taken_ = true;
}

void Battle::placeCapturePoint(const sim::Continent::City& town) {
    // In the courtyard before the keep: the town's defenders hold it at the
    // start (their garrison stands there), the player's army comes to take it.
    const sim::Vec3 at = town.castle.courtyard - region_->origin();
    capture_.place({at.x, at.y, sim::Fixed::zero()}, kEnemy, kPlayer);
}

void Battle::testStorm() {
    if (!capture_.active()) return;
    for (size_t g = 0; g < world_->gates().size(); ++g) world_->setGateHp(g, 0);
    const V3 at = toV3(capture_.at());
    for (const Squad& s : squads_)
        if (s.owner == kPlayer && aliveCount(s) > 0 && roleOf(s.type) != Role::Siege) moveSquad(s, at);
}

}  // namespace illyria::game
