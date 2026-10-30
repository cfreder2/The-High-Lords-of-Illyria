// Seats and their input streams for the matches (mode.h, docs/NETCODE.md).
#include "illyria/game/mode.h"

#include <algorithm>

#include "illyria/game/battle.h"

namespace illyria::game {

sim::Vec3 compass(int eighth) {
    const sim::Fixed o = sim::Fixed::one(), z = sim::Fixed::zero(), r = sim::Fixed::fromRatio(7071, 10000);
    switch (((eighth % 8) + 8) % 8) {
        case 0: return {o, z, z};
        case 1: return {r, r, z};
        case 2: return {z, o, z};
        case 3: return {-r, r, z};
        case 4: return {-o, z, z};
        case 5: return {-r, -r, z};
        case 6: return {z, -o, z};
        default: return {r, -r, z};
    }
}

sim::Vec3 insideField(const sim::Vec3& p, int32_t size, int32_t margin) {
    const sim::Fixed lo = sim::Fixed::fromInt(margin), hi = sim::Fixed::fromInt(size - margin);
    return {std::clamp(p.x, lo, hi), std::clamp(p.y, lo, hi), p.z};
}

V3 teamColour(sim::PlayerId team) {
    static const V3 kColours[4] = {
        {0.22f, 0.42f, 0.92f},  // blue: the renderer's player colour
        {0.86f, 0.18f, 0.14f},  // red: its enemy colour
        {0.92f, 0.72f, 0.16f},  // gold
        {0.22f, 0.64f, 0.30f},  // green
    };
    return kColours[team % 4];
}

const char* teamName(sim::PlayerId team) {
    static const char* kNames[4] = {"Blue", "Red", "Gold", "Green"};
    return kNames[team % 4];
}

void SeatedMode::gather(Battle& b, std::vector<sim::Command>& orders) {
    const uint32_t tick = b.world().tick();
    std::vector<sim::SeatInput> inputs;
    if (replaying_) {
        inputs = feed_.at(tick);
        orders.clear();
    } else {
        // The local seat: whatever the player did on the battlefield and the HUD.
        sim::SeatInput local;
        local.tick = tick;
        local.seat = static_cast<uint8_t>(localSeat_);
        local.orders = std::move(orders);
        local.actions = std::move(localActions_);
        orders.clear();
        localActions_.clear();
        if (localSeat_ >= 0 && localSeat_ < static_cast<int>(seats_.size()) && seats_[localSeat_].control == sim::SeatControl::Human)
            inputs.push_back(std::move(local));
        // The computer's seats: their orders land in `orders` (Battle's ai*
        // calls) and are sorted out by whose soldiers they are for.
        // TODO(netcode): only the host plays them, and a remote seat's input
        // for this tick comes from the relay (docs/NETCODE.md).
        if (tick % kThinkTicks == 0) think(b);
        std::vector<sim::SeatInput> computer(seats_.size());
        for (size_t s = 0; s < seats_.size(); ++s) {
            computer[s].tick = tick;
            computer[s].seat = static_cast<uint8_t>(s);
        }
        for (const sim::Command& c : orders) {
            const int s = roster_.seatOf(c.unit);
            if (s >= 0 && s < static_cast<int>(seats_.size()) && seats_[s].control == sim::SeatControl::Computer)
                computer[static_cast<size_t>(s)].orders.push_back(c);
        }
        orders.clear();
        for (const auto& [s, a] : computerActions_)
            if (s >= 0 && s < static_cast<int>(computer.size())) computer[static_cast<size_t>(s)].actions.push_back(a);
        computerActions_.clear();
        for (sim::SeatInput& in : computer)
            if (!in.empty()) inputs.push_back(std::move(in));
    }
    for (const sim::SeatInput& in : inputs) log_.record(in);
    orders = sim::mergeOrders(inputs, seats_, roster_);
    // The mode's actions, in seat order (as mergeOrders), after the orders.
    std::vector<const sim::SeatInput*> bySeat;
    for (const sim::SeatInput& in : inputs) bySeat.push_back(&in);
    std::stable_sort(bySeat.begin(), bySeat.end(), [](const sim::SeatInput* a, const sim::SeatInput* c) { return a->seat < c->seat; });
    for (const sim::SeatInput* in : bySeat)
        if (in->seat < seats_.size() && seats_[in->seat].playing())
            for (const sim::MatchAction& a : in->actions) applyAction(b, in->seat, a);
}

int SeatedMode::raise(Battle& b, int seat, const std::string& name, sim::PlayerId team, sim::UnitType type, int count,
                      V3 at, V3 facing, int rank) {
    const int q = b.addSquad(name, team, type, count, at, facing, rank);
    for (sim::EntityId id : b.squads()[static_cast<size_t>(q)].members) roster_.assign(id, seat);
    if (squadSeat_.size() <= static_cast<size_t>(q)) squadSeat_.resize(static_cast<size_t>(q) + 1, static_cast<int8_t>(sim::kNoSeat));
    squadSeat_[static_cast<size_t>(q)] = static_cast<int8_t>(seat);
    // Seated by someone else: not the local player's to order (an ally's, or a foe's).
    b.setForeign(static_cast<size_t>(q), seat != localSeat_);
    return q;
}

int SeatedMode::seatFor(int wanted) const {
    const int n = static_cast<int>(std::min<size_t>(seats_.size(), sim::kMaxSeats));
    if (wanted >= 0 && wanted < n && seats_[static_cast<size_t>(wanted)].playing()) return wanted;
    for (int s = 0; s < n; ++s)
        if (seats_[static_cast<size_t>(s)].control == sim::SeatControl::Human) return s;
    return 0;
}

void SeatedMode::setLocalSeat(Battle& b, int seat) {
    if (seat < 0 || seat >= static_cast<int>(seats_.size())) return;
    localSeat_ = seat;
    b.setLocalSide(seats_[static_cast<size_t>(seat)].team);
    for (size_t q = 0; q < squadSeat_.size(); ++q) b.setForeign(q, squadSeat_[q] != seat);
}

void SeatedMode::hashSeats(sim::StateHasher& h) const {
    h.addU64(seats_.size());
    for (const sim::Seat& s : seats_) {
        h.addU64(static_cast<uint64_t>(s.control));
        h.addU64(s.team);
    }
    roster_.hash(h);
}

}  // namespace illyria::game
