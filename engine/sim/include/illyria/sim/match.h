#pragma once

// Seats, teams and each seat's stream of input in a match (docs/NETCODE.md).
//
// A match (Horde, capture the flag) is played by up to four seats. Each seat
// is a person, the computer or nobody; it sits on a team (the simulation's
// PlayerId: soldiers of one team are friends) and owns its own soldiers.
// Everything a seat does reaches the simulation as its SeatInput for a tick:
// orders to its own soldiers and the mode's actions (a reinforcement picked).
// Lockstep peers exchange exactly these. The computer's seats are played by
// one peer (the host) and their inputs travel like anyone's, so the computer
// players' own arithmetic never has to agree between machines.

#include <cstdint>
#include <vector>

#include "illyria/sim/hash.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

inline constexpr int kMaxSeats = 4;
inline constexpr int kNoSeat = -1;

enum class SeatControl : uint8_t { Empty, Human, Computer };
struct Seat {
    SeatControl control = SeatControl::Empty;
    PlayerId team = 0;
    bool playing() const { return control != SeatControl::Empty; }
};

// What a seat does beyond ordering its soldiers.
struct MatchAction {
    // Recruit: buys one of the round's offers (Horde's reinforcements).
    // Ready: done with the break; the next wave may come.
    // Recall: pays to have the seat's fallen hero back at once (Horde).
    enum class Kind : uint8_t { Recruit, Ready, Recall };
    Kind kind = Kind::Recruit;
    uint8_t option = 0;  // Recruit: the offer's index
};

// One seat's input for one tick.
struct SeatInput {
    uint32_t tick = 0;
    uint8_t seat = 0;
    std::vector<Command> orders;
    std::vector<MatchAction> actions;
    bool empty() const { return orders.empty() && actions.empty(); }
};
void hashInput(StateHasher& h, const SeatInput& in);

// Every seat's inputs, tick by tick: what lockstep peers exchange, and a
// replay of the whole match. Ticks without input are not kept.
class InputLog {
public:
    // In (tick, seat) order: a tick's inputs arrive in any seat order, but
    // never for a tick already passed.
    void record(SeatInput in);
    // The inputs for a tick, in seat order.
    std::vector<SeatInput> at(uint32_t tick) const;
    const std::vector<SeatInput>& inputs() const { return inputs_; }
    uint64_t checksum() const;

private:
    std::vector<SeatInput> inputs_;
};

// Which seat owns each soldier (kNoSeat: none of them, as the horde).
class SeatRoster {
public:
    void assign(EntityId id, int seat);
    int seatOf(EntityId id) const;
    void hash(StateHasher& h) const;

private:
    std::vector<int8_t> seat_;  // by EntityId - 1
};

// The tick's orders for the world, merged in seat order. A seat's orders
// count only for its own soldiers, given as its team (a peer cannot order
// another's men, nor the enemy's).
std::vector<Command> mergeOrders(const std::vector<SeatInput>& inputs, const std::vector<Seat>& seats,
                                 const SeatRoster& roster);

}  // namespace illyria::sim
