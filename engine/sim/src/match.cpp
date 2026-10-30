// Seats and their input streams (match.h, docs/NETCODE.md).
#include "illyria/sim/match.h"

#include <algorithm>

namespace illyria::sim {

namespace {

void hashVec(StateHasher& h, const Vec3& v) {
    h.addI64(v.x.raw);
    h.addI64(v.y.raw);
    h.addI64(v.z.raw);
}

}  // namespace

void hashInput(StateHasher& h, const SeatInput& in) {
    h.addU64(in.tick);
    h.addU64(in.seat);
    h.addU64(in.orders.size());
    for (const Command& c : in.orders) {
        h.addU64(c.player);
        h.addU64(c.unit);
        h.addU64(static_cast<uint64_t>(c.type));
        hashVec(h, c.target);
        h.addI64(c.speed.raw);
        h.addU64(c.march);
        hashVec(h, c.marchFrom);
        hashVec(h, c.marchTo);
        hashVec(h, c.face);
        h.addU64(c.force);
        h.addU64(c.targetUnit);
        h.addU64(c.rally);
        h.addU64(static_cast<uint64_t>(c.object));
        h.addI64(c.objectIndex);
    }
    h.addU64(in.actions.size());
    for (const MatchAction& a : in.actions) {
        h.addU64(static_cast<uint64_t>(a.kind));
        h.addU64(a.option);
    }
}

void InputLog::record(SeatInput in) {
    if (in.empty()) return;
    const auto before = [](const SeatInput& a, const SeatInput& b) { return a.tick != b.tick ? a.tick < b.tick : a.seat < b.seat; };
    auto at = std::upper_bound(inputs_.begin(), inputs_.end(), in, before);
    // Two inputs of one seat in one tick (an order and a pick): one input.
    if (at != inputs_.begin() && std::prev(at)->tick == in.tick && std::prev(at)->seat == in.seat) {
        SeatInput& prev = *std::prev(at);
        prev.orders.insert(prev.orders.end(), in.orders.begin(), in.orders.end());
        prev.actions.insert(prev.actions.end(), in.actions.begin(), in.actions.end());
        return;
    }
    inputs_.insert(at, std::move(in));
}

std::vector<SeatInput> InputLog::at(uint32_t tick) const {
    const auto lo = std::lower_bound(inputs_.begin(), inputs_.end(), tick, [](const SeatInput& a, uint32_t t) { return a.tick < t; });
    std::vector<SeatInput> out;
    for (auto it = lo; it != inputs_.end() && it->tick == tick; ++it) out.push_back(*it);
    return out;
}

uint64_t InputLog::checksum() const {
    StateHasher h;
    h.addU64(inputs_.size());
    for (const SeatInput& in : inputs_) hashInput(h, in);
    return h.value();
}

void SeatRoster::assign(EntityId id, int seat) {
    if (id == kNoEntity) return;
    if (seat_.size() < id) seat_.resize(id, static_cast<int8_t>(kNoSeat));
    seat_[id - 1] = static_cast<int8_t>(seat);
}

int SeatRoster::seatOf(EntityId id) const {
    return id != kNoEntity && id <= seat_.size() ? seat_[id - 1] : kNoSeat;
}

void SeatRoster::hash(StateHasher& h) const {
    h.addU64(seat_.size());
    for (int8_t s : seat_) h.addI64(s);
}

std::vector<Command> mergeOrders(const std::vector<SeatInput>& inputs, const std::vector<Seat>& seats,
                                 const SeatRoster& roster) {
    std::vector<const SeatInput*> order;
    for (const SeatInput& in : inputs) order.push_back(&in);
    std::stable_sort(order.begin(), order.end(), [](const SeatInput* a, const SeatInput* b) { return a->seat < b->seat; });
    std::vector<Command> out;
    for (const SeatInput* in : order) {
        if (in->seat >= seats.size() || !seats[in->seat].playing()) continue;
        for (Command c : in->orders) {
            if (roster.seatOf(c.unit) != in->seat) continue;
            c.player = seats[in->seat].team;
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace illyria::sim
