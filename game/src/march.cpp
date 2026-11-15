// The March's book between battles (march.h): who is left, what they have done, the purse, and the next battle.
#include "illyria/game/march.h"

#include <algorithm>
#include <cstdlib>

namespace illyria::game {

using sim::UnitType;

const March::Stage& March::stageInfo(int stage) {
    // THE MARCH'S TUNING, all of it (with kPursePct, kHealShare and the two k...Battles in march.h). A field, a village
    // stormed, the Ford, a village held, the capital, against an army that only shrinks. `foePct` is the purse the
    // battle's odds are reckoned from, in parts of a hundred of the army's cost (in a siege the keepers raise two
    // thirds of it and the besiegers half as much again: Battle::kSiegeOddsPct); `siegeSeconds` the time the player
    // has to take a town he besieges. To try a curve without a build: ILLYRIA_MARCH_PCT (docs/PLAYTEST.md).
    //
    // Why this order: the village is second because it is the first battle that asks for an order. Measured by script
    // (docs/GAME_DESIGN.md §3.7), a host that is given no order wins the open field against any purse a host that is
    // given orders can also beat. Before a wall it only stands, and the siege's time runs out: a march without orders
    // ends at battle two.
    //
    // The Ford is the flag on the isle (docs/GAME_DESIGN.md §4.1.4): fair ground, no bank to defend itself, and its
    // own clock (a flag left alone a minute is the battle, so it has no siegeSeconds). A host given no order loses it
    // in about 100 s. Its foe has 80 parts in a hundred of the army's cost, not the 100 it had behind a river: the
    // army that comes to it has fought twice. NOT YET PLAYED THROUGH BY SCRIPT at this figure (s-none, s-plan).
    static const Stage kStages[kBattles] = {{"The open field", Scenario::Field, false, 100, 0},
                                            {"The village", Scenario::Siege, false, 45, 8 * 60},
                                            {"The Ford", Scenario::Ford, false, 80, 0},
                                            {"The defence", Scenario::Siege, true, 40, 0},
                                            {"The capital", Scenario::Capital, false, 45, 13 * 60}};
    return kStages[std::clamp(stage, 0, kBattles - 1)];
}

bool March::irreplaceable(UnitType type) {
    return type == UnitType::Hero || type == UnitType::Wizards || type == UnitType::DwarfGrenadier || !sim::Conquest::trainable(type);
}

March March::begin(const std::vector<Buy>& army, bool easy) {
    March m;
    m.budget = std::max(1, armyCost(army));
    m.easy = easy;
    const auto add = [&](const std::string& name, UnitType type, int men) {
        Squad s{name, type, men, {}, 0, 0};
        s.men.assign(static_cast<size_t>(men), Man{});
        m.squads.push_back(std::move(s));
    };
    for (const Buy& b : army)
        // (Within the caps on those who throw a blast, whatever the screen sent: Conquest::keepCap.)
        for (int i = 0; i < (sim::Conquest::keepCap(b.type) > 0 ? std::min(b.squads, sim::Conquest::keepCap(b.type)) : b.squads); ++i)
            add(std::string(sim::unitTypeName(b.type)) + (b.squads > 1 ? " " + std::to_string(i + 1) : ""), b.type, sim::Conquest::squadSize(b.type));
    add("The Lord", UnitType::Hero, 1);
    add("Magus of the Tower", UnitType::Wizards, sim::Conquest::squadSize(UnitType::Wizards));
    return m;
}

int March::men() const {
    int n = 0;
    for (const Squad& s : squads) n += static_cast<int>(s.men.size());
    return n;
}

SkirmishSetup March::setup() const {
    const Stage& st = stageInfo(stage);
    SkirmishSetup s;
    s.easy = easy;
    s.budget = budget;
    s.defend = st.defend;
    s.foePurse = std::max(1, budget * st.foePct / 100);
    s.siegeSeconds = st.siegeSeconds;
    for (const Squad& q : squads) {
        CarriedSquad c{q.name, q.type, q.full, {}};
        for (size_t i = 0; i < q.men.size(); ++i) c.men.push_back({q.men[i].hp, q.men[i].rank, q.men[i].kills, static_cast<int>(i), q.men[i].satchels, q.men[i].ammo});
        s.roster.push_back(std::move(c));
    }
    return s;
}

void March::settle(int outcome, const std::vector<CarriedSquad>& after) {
    if (over() || (outcome != 1 && outcome != 2)) return;
    const char* where = stageInfo(stage).name;
    last = {};
    std::vector<Squad> left;
    for (size_t i = 0; i < squads.size(); ++i) {
        Squad& q = squads[i];
        // By name (a battle ranks the squads by kind, not in the book's order); one the battle does not tell of is gone.
        const auto it = std::find_if(after.begin(), after.end(), [&](const CarriedSquad& c) { return c.name == q.name; });
        const CarriedSquad* a = it != after.end() ? &*it : nullptr;
        const int before = static_cast<int>(q.men.size());
        std::vector<Man> men;
        int promoted = 0, gained = 0;  // gained: what the living killed in this battle (the squad's tally keeps the fallen's from before)
        if (a) {
            for (const CarriedMan& c : a->men) {
                if (c.slot < 0 || c.slot >= before) continue;
                Man m = q.men[static_cast<size_t>(c.slot)];
                const int was = m.rank;
                m.hp = std::clamp(c.hp, 1, 1000);
                m.hp += (1000 - m.hp) / kHealShare;  // wounds partly mended on the road
                gained += std::max(0, c.kills - m.kills);
                m.kills = std::max(m.kills, c.kills);
                m.rank = std::max(m.rank, c.rank);
                m.satchels = c.satchels;  // what he laid and threw stays spent until the camp makes it up
                m.ammo = c.ammo;
                ++m.battles;
                // Living through battles makes a soldier too (a hero's level is his kills' alone).
                if (q.type != UnitType::Hero) m.rank = std::max(m.rank, m.battles >= kVeteranBattles ? 2 : m.battles >= kSeasonedBattles ? 1 : 0);
                promoted += m.rank > was;
                men.push_back(m);
            }
        }
        q.kills += gained;
        ++q.battles;
        last.lost += before - static_cast<int>(men.size());
        last.survivors += static_cast<int>(men.size());
        last.promoted += promoted;
        if (promoted > 0) last.promotions.push_back(q.name + ": " + std::to_string(promoted) + " promoted");
        q.men = std::move(men);
        if (q.men.empty()) {
            last.gone.push_back(q.name);
            gone.push_back(q.name + " (" + where + ")");
        } else {
            left.push_back(std::move(q));
        }
    }
    squads = std::move(left);
    if (outcome != 1 || squads.empty()) {
        lost = true;
        return;
    }
    ++stage;
    if (stage < kBattles) {
        last.gold = std::max(1, budget * kPursePct / 100);
        purse += last.gold;
    }
}

namespace {
int menCost(const March& m, size_t squad) {
    const March::Squad& q = m.squads[squad];
    const int missing = q.full - static_cast<int>(q.men.size());
    if (missing <= 0 || March::irreplaceable(q.type)) return 0;
    return (sim::Conquest::price(q.type) * missing + q.full - 1) / q.full;
}
}  // namespace

void March::load(size_t squad, int& carried, int& full) const {
    carried = full = 0;
    if (squad >= squads.size()) return;
    const Squad& q = squads[squad];
    const sim::UnitStats st = sim::unitStats(q.type);
    const int each = st.satchels.count + st.ranged.ammo;
    for (const Man& man : q.men) {
        full += each;
        carried += (man.satchels < 0 ? st.satchels.count : std::min(man.satchels, st.satchels.count)) + (man.ammo < 0 ? st.ranged.ammo : std::min(man.ammo, st.ranged.ammo));
    }
}

int March::resupplyCost(size_t squad) const {
    if (over() || squad >= squads.size()) return 0;
    int carried, full;
    load(squad, carried, full);
    if (full <= 0 || carried >= full) return 0;
    // Half the squad's price for a whole squad's whole load, in proportion (a pair of dwarves' six satchels and six bottles: 30 gold).
    const Squad& q = squads[squad];
    const int whole = std::max(1, full / std::max<int>(1, static_cast<int>(q.men.size())) * q.full * 2);
    return std::max(1, (sim::Conquest::price(q.type) * (full - carried) + whole - 1) / whole);
}

bool March::resupply(size_t squad) {
    const int cost = resupplyCost(squad);
    if (cost <= 0 || purse < cost) return false;
    purse -= cost;
    for (Man& man : squads[squad].men) man.satchels = man.ammo = -1;
    return true;
}

int March::reinforceCost(size_t squad) const {
    if (over() || squad >= squads.size()) return 0;
    return menCost(*this, squad) + resupplyCost(squad);
}

int March::reinforce(size_t squad) {
    if (over() || squad >= squads.size()) return 0;
    // The load first (it is the dearer loss: a dwarf with nothing to lay), then the men.
    const int made = resupply(squad) ? 1 : 0;
    const int cost = menCost(*this, squad);
    if (cost <= 0 || purse <= 0) return made;
    Squad& q = squads[squad];
    const int missing = q.full - static_cast<int>(q.men.size());
    // All of them if the purse runs to it, else as many as it pays for.
    const int joined = purse >= cost ? missing : static_cast<int>(static_cast<int64_t>(purse) * q.full / std::max(1, sim::Conquest::price(q.type)));
    if (joined <= 0) return made;
    purse -= joined == missing ? cost : (sim::Conquest::price(q.type) * joined + q.full - 1) / q.full;
    q.men.insert(q.men.end(), static_cast<size_t>(joined), Man{});
    return joined;
}

int March::reinforceAll() {
    int joined = 0;
    for (;;) {
        size_t pick = squads.size();
        for (size_t i = 0; i < squads.size(); ++i) {
            if (reinforceCost(i) <= 0) continue;
            if (pick == squads.size() || static_cast<int64_t>(squads[i].men.size()) * squads[pick].full < static_cast<int64_t>(squads[pick].men.size()) * squads[i].full) pick = i;
        }
        const int n = pick < squads.size() ? reinforce(pick) : 0;
        if (n <= 0) return joined;
        joined += n;
    }
}

// The save: "march1;budget;easy;stage;purse;lost" then a field a squad, "S|name|type|full|kills|battles|hp.rank.kills.battles,..."
// and a field a squad gone, "G|name (where)". Names hold letters, digits, spaces and brackets only.
std::string March::save() const {
    std::string s = "march1;" + std::to_string(budget) + ";" + std::to_string(easy ? 1 : 0) + ";" + std::to_string(stage) + ";" + std::to_string(purse) + ";" + std::to_string(lost ? 1 : 0);
    for (const Squad& q : squads) {
        s += ";S|" + q.name + "|" + std::to_string(static_cast<int>(q.type)) + "|" + std::to_string(q.full) + "|" + std::to_string(q.kills) + "|" + std::to_string(q.battles) + "|";
        for (size_t i = 0; i < q.men.size(); ++i) {
            const Man& m = q.men[i];
            s += (i ? "," : "") + std::to_string(m.hp) + "." + std::to_string(m.rank) + "." + std::to_string(m.kills) + "." + std::to_string(m.battles);
            // (What he carries, only once something is spent: two more numbers, his satchels and his missiles left. A line
            // written before loads were kept has four, and reads as a full load.)
            if (m.satchels >= 0 || m.ammo >= 0) {
                const sim::UnitStats st = sim::unitStats(q.type);
                s += "." + std::to_string(m.satchels < 0 ? st.satchels.count : m.satchels) + "." + std::to_string(m.ammo < 0 ? st.ranged.ammo : m.ammo);
            }
        }
    }
    for (const std::string& g : gone) s += ";G|" + g;
    return s;
}

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t from = 0;
    for (;;) {
        const size_t at = s.find(sep, from);
        out.push_back(s.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) return out;
        from = at + 1;
    }
}

bool number(const std::string& s, int lo, int hi, int& out) {
    if (s.empty() || s.size() > 9) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    out = std::atoi(s.c_str());
    return out >= lo && out <= hi;
}

bool nameOk(const std::string& s) {
    if (s.empty() || s.size() > 60) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '(' || c == ')' || c == '\'' || c == '-')) return false;
    return true;
}

}  // namespace

bool March::load(const std::string& text, March& out) {
    const std::vector<std::string> f = split(text, ';');
    March m;
    int easy = 0, lost = 0;
    if (f.size() < 6 || f[0] != "march1" || !number(f[1], 1, 100000, m.budget) || !number(f[2], 0, 1, easy) || !number(f[3], 0, kBattles, m.stage) ||
        !number(f[4], 0, 100000, m.purse) || !number(f[5], 0, 1, lost))
        return false;
    m.easy = easy != 0;
    m.lost = lost != 0;
    for (size_t i = 6; i < f.size(); ++i) {
        const std::vector<std::string> p = split(f[i], '|');
        if (p[0] == "G" && p.size() == 2 && nameOk(p[1])) {
            m.gone.push_back(p[1]);
            continue;
        }
        if (p[0] != "S" || p.size() != 7 || !nameOk(p[1])) return false;
        Squad q;
        int type = 0;
        q.name = p[1];
        if (!number(p[2], 0, static_cast<int>(UnitType::Count) - 1, type) || !number(p[3], 1, 200, q.full) || !number(p[4], 0, 1000000, q.kills) || !number(p[5], 0, 1000, q.battles)) return false;
        q.type = static_cast<UnitType>(type);
        for (const std::string& man : split(p[6], ',')) {
            const std::vector<std::string> v = split(man, '.');
            Man one;
            if ((v.size() != 4 && v.size() != 6) || !number(v[0], 1, 1000, one.hp) || !number(v[1], 0, 8, one.rank) || !number(v[2], 0, 1000000, one.kills) || !number(v[3], 0, 1000, one.battles)) return false;
            if (v.size() == 6 && (!number(v[4], 0, 1000, one.satchels) || !number(v[5], 0, 1000, one.ammo))) return false;
            q.men.push_back(one);
        }
        if (q.men.empty() || static_cast<int>(q.men.size()) > q.full) return false;
        m.squads.push_back(std::move(q));
    }
    if (m.squads.empty() && !m.lost) return false;
    out = std::move(m);
    return true;
}

}  // namespace illyria::game
