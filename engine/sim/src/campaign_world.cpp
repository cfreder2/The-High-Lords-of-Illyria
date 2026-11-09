// The men on the land (docs/ONE_WORLD.md, "Armies on the land" and "Fights").
//
// Every squad is its real men, and every man on the land is a unit of the one sim::World the campaign holds on the
// ground of the whole island, with all eighty castles in it: the same World, the same navigation, steering, ramps,
// gates, formations, orders and fights as a skirmish, on a ground without a fence. There is no other way a man moves.
// The stacks are only the groups that march together and the unit the computer lords plan with: every
// kThinkTicks a stack looks where its men are, sends them on to the next cell of its route, and meets whoever comes
// near. A man aboard a boat is out of the world (the ship carries him) and is put ashore as a unit where it lands.
//
// Nothing is settled by dice: men die, towns fall by banners raised over them (campaign_capture.cpp).
#include <algorithm>
#include <cmath>
#include <map>

#include "illyria/sim/campaign.h"
#include "illyria/sim/isqrt.h"

namespace illyria::sim {

namespace {

constexpr int32_t kIslandCells = 900;  // the island's ground, 3.6 km of 4 m cells (the map is 2.8 x 3.6 km)
constexpr uint32_t kCorpseTicks = 600 * kTicksPerSecond;  // the dead lie as long as the fight lasts, as in the battle (ten minutes at most)
constexpr int64_t kAboutMm = 25000;  // ... or most of the men are this near it
constexpr int64_t kArriveMm = 12000;  // marching, a cell is reached within this of its anchor
constexpr int64_t kHoldMm = 45000;  // men standing meet whoever gets this near their post
constexpr int32_t kAbreast = 4;
constexpr int32_t kSideBySideMm = 11000;

int32_t toMm(Fixed metres) { return static_cast<int32_t>((metres.raw * 1000 + Fixed::kOneRaw / 2) >> Fixed::kFracBits); }
Fixed fromMm(int64_t mm) { return Fixed::fromRatio(mm, 1000); }
// Thousandths of a unit vector, for a direction given in any units.
void unitOf(int64_t dx, int64_t dy, int32_t& ux, int32_t& uy) {
    const int64_t len = isqrt(dx * dx + dy * dy);
    if (len == 0) return;
    ux = static_cast<int32_t>(dx * 1000 / len);
    uy = static_cast<int32_t>(dy * 1000 / len);
}
uint32_t hash32(uint32_t v) {
    v *= 2654435761u;
    v ^= v >> 15;
    v *= 2246822519u;
    v ^= v >> 13;
    return v;
}

// The nearest place to p that fits, a ring at a time (round the compass in sixths of a turn per metre of radius), or p.
template <class Fits>
Vec3 nearestFit(const Vec3& p, const Fits& fits) {
    for (int ring = 0; ring <= 40 && !fits(p); ++ring) {
        for (int step = 0; step < std::max(1, 6 * ring); ++step) {
            static const int32_t kCos[24] = {1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259, 0, 259, 500, 707, 866, 966};
            static const int32_t kSin[24] = {0, 259, 500, 707, 866, 966, 1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259};
            const int32_t dir = step * 24 / std::max(1, 6 * ring);
            const Vec3 q{p.x + Fixed::fromRatio(kCos[dir] * ring, 1000), p.y + Fixed::fromRatio(kSin[dir] * ring, 1000), p.z};
            if (fits(q)) return q;
        }
    }
    return p;
}

// The island's ground, made once for every campaign on a continent: it is a
// pure function of the continent, and a few seconds to make.
std::shared_ptr<const Region> islandRegion(const Continent& c) {
    static std::map<uint64_t, std::pair<const Continent*, std::shared_ptr<const Region>>> made;
    auto it = made.find(c.seed());
    if (it != made.end() && it->second.first == &c) return it->second.second;
    const Vec3 centre{Fixed::fromInt(Continent::kWidth / 2), Fixed::fromInt(Continent::kHeight / 2), Fixed::zero()};
    auto region = std::make_shared<const Region>(Region::fromContinent(c, centre, kIslandCells));
    made[c.seed()] = {&c, region};
    return region;
}

}  // namespace

// ---------------------------------------------------------------- where things are

void Campaign::anchorOf(int32_t cx, int32_t cy, int32_t& x, int32_t& y) const {
    if (cx >= 0 && cy >= 0 && cx < nx_ && cy < ny_ && terrain_[static_cast<size_t>(cy) * nx_ + cx] == Lay::City) {
        const int32_t town = cityAt(cx, cy);
        if (town >= 0) {
            // Its courtyard, where the walls' defenders stand (the keep's own ground is built on).
            const Continent::City& city = continent_.cities()[static_cast<size_t>(town)];
            const Vec3& at = city.castle.courtyardRadius > Fixed::zero() ? city.castle.courtyard : city.pos;
            x = toMm(at.x);
            y = toMm(at.y);
            return;
        }
    }
    x = centre(cx) * 1000;
    y = centre(cy) * 1000;
}

void Campaign::bodyOf(const Stack& s, int32_t ahead, int32_t& x, int32_t& y) const {
    if (!s.boat) {
        x = s.px;
        y = s.py;
        return;
    }
    x = centre(s.cx) * 1000;
    y = centre(s.cy) * 1000;
    if (s.path.empty()) return;
    const int32_t leg = legTime(s);
    if (leg <= 0 || leg == kClosed) return;
    const int32_t tx = centre(s.path.front().first) * 1000, ty = centre(s.path.front().second) * 1000;
    const int64_t t = std::clamp<int64_t>(int64_t{s.progress} + ahead, 0, leg);
    x += static_cast<int32_t>((int64_t{tx} - x) * t / leg);
    y += static_cast<int32_t>((int64_t{ty} - y) * t / leg);
}

void Campaign::layout(const Stack& s, std::vector<std::pair<int32_t, int32_t>>& slots, bool column) const {
    slots.clear();
    struct Block {
        int32_t n = 0, cols = 1, rows = 0, spacing = 1400, depth = 0, width = 0;
    };
    std::vector<Block> blocks;
    for (const Army& a : s.armies) {
        Block b;
        for (const Man& m : a.men) b.n += m.alive();
        if (b.n > 0) {
            b.spacing = std::max(1400, toMm(unitStats(a.type).radius) * 12 / 5);
            b.cols = std::min(b.n, kAbreast);
            b.rows = (b.n + b.cols - 1) / b.cols;
            b.depth = (b.rows - 1) * b.spacing * 5 / 4;
            b.width = (b.cols - 1) * b.spacing;
        }
        blocks.push_back(b);
    }
    int32_t live = 0;
    for (const Block& b : blocks) live += b.n > 0;
    // Marching, a column of squads, the first in front; standing, a block of them.
    const int32_t perRow = column ? 1 : std::max<int32_t>(1, static_cast<int32_t>(isqrt(live - 1)) + 1);
    std::vector<int32_t> rowDepth, colWidth(static_cast<size_t>(perRow), 0);
    {
        int32_t k = 0;
        for (const Block& b : blocks) {
            if (b.n == 0) continue;
            const size_t row = static_cast<size_t>(k / perRow);
            if (rowDepth.size() <= row) rowDepth.resize(row + 1, 0);
            rowDepth[row] = std::max(rowDepth[row], b.depth);
            colWidth[static_cast<size_t>(k % perRow)] = std::max(colWidth[static_cast<size_t>(k % perRow)], b.width);
            ++k;
        }
    }
    constexpr int32_t kGap = 3000;
    int32_t length = 0;
    for (int32_t d : rowDepth) length += d + kGap;
    length -= kGap;
    int32_t widthAll = 0;
    for (int32_t w : colWidth) widthAll += w + kGap;
    widthAll -= kGap;
    int32_t k = 0;
    for (size_t ai = 0; ai < s.armies.size(); ++ai) {
        const Block& b = blocks[ai];
        if (b.n == 0) continue;
        const size_t row = static_cast<size_t>(k / perRow), col = static_cast<size_t>(k % perRow);
        int32_t front = length / 2;
        for (size_t r = 0; r < row; ++r) front -= rowDepth[r] + kGap;
        int32_t left = -widthAll / 2;
        for (size_t c = 0; c < col; ++c) left += colWidth[c] + kGap;
        const int32_t centreU = left + colWidth[col] / 2;
        ++k;
        int32_t i = 0;
        for (const Man& m : s.armies[ai].men) {
            if (!m.alive()) continue;
            const int32_t r = i / b.cols, c = i % b.cols;
            const int32_t inRow = r == b.rows - 1 ? b.n - r * b.cols : b.cols;
            int32_t u = centreU + (2 * c - (inRow - 1)) * b.spacing / 2;
            int32_t v = front - r * b.spacing * 5 / 4;
            // A man a little off his slot by his number, so that a rank is not ruled.
            u += static_cast<int32_t>(hash32(m.id) % 440u) - 220;
            v += static_cast<int32_t>(hash32(m.id + 7919u) % 440u) - 220;
            slots.push_back({u, v});
            ++i;
        }
    }
}

namespace {
// A slot in the stack's frame (right, forward) as an offset east and north.
void slotToWorld(const std::pair<int32_t, int32_t>& slot, int32_t hx, int32_t hy, int32_t& dx, int32_t& dy) {
    dx = static_cast<int32_t>((int64_t{hx} * slot.second + int64_t{hy} * slot.first) / 1000);
    dy = static_cast<int32_t>((int64_t{hy} * slot.second - int64_t{hx} * slot.first) / 1000);
}
}  // namespace

void Campaign::soldiers(int32_t ahead, std::vector<Soldier>& out) const {
    out.clear();
    const float alpha = std::clamp(static_cast<float>(ahead) / kTimeScale, 0.0f, 1.0f);
    for (const Stack& s : stacks_) {
        if (s.armies.empty() || s.boat) continue;  // (aboard a boat a man is out of the world)
        for (const Army& a : s.armies)
            for (const Man& m : a.men) {
                if (!m.unit) continue;
                Soldier d;
                d.id = m.id;
                d.stack = s.id;
                d.army = a.id;
                d.owner = s.owner;
                d.type = a.type;
                d.alive = m.hp != 0;
                if (!d.alive && (!world_ || !world_->findUnit(m.unit))) continue;
                d.x = (static_cast<float>(m.bx) + static_cast<float>(m.ax - m.bx) * alpha) / 1000.0f;
                d.y = (static_cast<float>(m.by) + static_cast<float>(m.ay - m.by) * alpha) / 1000.0f;
                d.yaw = std::atan2(static_cast<float>(m.fy), static_cast<float>(m.fx));
                d.speed = d.alive && (m.ax != m.bx || m.ay != m.by)
                              ? std::hypot(static_cast<float>(m.ax - m.bx), static_cast<float>(m.ay - m.by)) / 1000.0f * kTicksPerSecond
                              : 0.0f;
                d.aloft = d.alive && flies(a.type) && world_ && world_->findUnit(m.unit) && world_->findUnit(m.unit)->flight == Unit::Flight::Flying;
                d.unit = world_ ? world_->findUnit(m.unit) : nullptr;
                d.unitId = m.unit;
                d.struckTick = m.struckTick;
                d.hurtTick = m.hurtTick;
                d.fightTick = m.fightTick;
                out.push_back(d);
            }
    }
}

void Campaign::visibleSoldiers(uint8_t viewer, int32_t ahead, std::vector<Soldier>& out) const {
    soldiers(ahead, out);
    std::erase_if(out, [&](const Soldier& m) { return !canSee(viewer, m.owner, m.x, m.y); });
}

bool Campaign::seesCity(uint8_t viewer, int32_t city) const {
    if (city < 0 || city >= static_cast<int32_t>(cities_.size())) return false;
    const Vec3& at = continent_.cities()[static_cast<size_t>(city)].pos;
    return canSee(viewer, cities_[static_cast<size_t>(city)].owner, static_cast<float>(at.x.toDouble()), static_cast<float>(at.y.toDouble()));
}

int32_t Campaign::menInWorld() const {
    int32_t n = 0;
    for (const Stack& s : stacks_)
        for (const Army& a : s.armies)
            for (const Man& m : a.men) n += m.alive() && m.unit != 0;
    return n;
}

// ---------------------------------------------------------------- the world

void Campaign::ensureWorld() {
    if (world_) return;
    region_ = islandRegion(continent_);
    WorldConfig cfg;
    cfg.seed = seed_ ^ 0x5A17E;
    cfg.region = region_;
    world_ = std::make_unique<World>(cfg);
    // Each gate is some town's: its holder does not hack at it, and a stronger wall holds longer.
    const Vec3 origin = region_->origin();
    gateCity_.assign(region_->gates().size(), -1);
    for (size_t g = 0; g < gateCity_.size(); ++g) {
        const Gate& gate = region_->gates()[g];
        const Vec3 at = (gate.a + gate.b) * Fixed::fromRatio(1, 2) + origin;
        Fixed best = Fixed::fromInt(1000000);
        for (size_t i = 0; i < continent_.cities().size(); ++i) {
            const Fixed d = (continent_.cities()[i].pos - at).flat().length();
            if (d < best) {
                best = d;
                gateCity_[g] = static_cast<int32_t>(i);
            }
        }
    }
    for (size_t i = 0; i < cities_.size(); ++i) holdGates(static_cast<int32_t>(i));
    // And the wounds a saved game had given them.
    for (size_t k = 0; k + 1 < gateHp_.size(); k += 2) world_->setGateHp(static_cast<size_t>(gateHp_[k]), gateHp_[k + 1]);
    gateHp_.clear();
    for (size_t k = 0; k + 1 < gatePin_.size(); k += 2) world_->setGatePin(static_cast<size_t>(gatePin_[k]), static_cast<Command::GatePin>(gatePin_[k + 1]));
    gatePin_.clear();
    endFight(seed_);
}

void Campaign::prepareWorld() {
    ensureWorld();
    seatNew();
}

void Campaign::gateHps(std::vector<int32_t>& out) const {
    // The gates that are not as new, as (gate, hit points left) pairs: all that
    // the world holds between fights, so the same whether or not there has been one.
    out.clear();
    if (!world_) {
        out = gateHp_;
        return;
    }
    for (size_t g = 0; g < world_->gates().size(); ++g)
        if (world_->gates()[g].hp != region_->gates()[g].maxHp) {
            out.push_back(static_cast<int32_t>(g));
            out.push_back(std::max(0, world_->gates()[g].hp));
        }
}

void Campaign::setHuman(uint8_t side, bool human) {
    sides_[side].human = human;
    if (world_)
        for (size_t i = 0; i < cities_.size(); ++i) holdGates(static_cast<int32_t>(i));
}

void Campaign::gatePinList(std::vector<int32_t>& out) const {
    out.clear();
    if (!world_) {
        out = gatePin_;
        return;
    }
    for (size_t g = 0; g < world_->gates().size(); ++g)
        if (world_->gates()[g].pin != Command::GatePin::Auto) {
            out.push_back(static_cast<int32_t>(g));
            out.push_back(static_cast<int32_t>(world_->gates()[g].pin));
        }
}

void Campaign::holdGates(int32_t city) {
    if (!world_) return;
    for (size_t g = 0; g < gateCity_.size(); ++g)
        if (gateCity_[g] == city) {
            const uint8_t owner = cities_[static_cast<size_t>(city)].owner;
            world_->setGateHolder(g, owner);
            // A neutral town's gate and a computer lord's are kept by their garrison (World::keepGates); the player's by his hand.
            world_->setGateGarrisoned(g, owner == kNeutral || !sides_[owner].human);
        }
}

void Campaign::seat(Stack& s) {
    ensureWorld();
    const Vec3 origin = region_->origin();
    // Where it stands if it has no place yet: its cell's anchor, set beside the stacks already standing there.
    int32_t bx, by;
    anchorOf(s.cx, s.cy, bx, by);
    if (s.path.empty() && !s.boat) {
        int32_t rank = 0;
        for (const Stack& o : stacks_) {
            if (&o == &s) break;
            rank += !o.armies.empty() && o.path.empty() && !o.boat && o.cx == s.cx && o.cy == s.cy;
        }
        if (rank > 0) {
            const int32_t side = (rank % 2 ? 1 : -1) * ((rank + 1) / 2) * kSideBySideMm;
            bx += static_cast<int32_t>(int64_t{s.hy} * side / 1000);
            by -= static_cast<int32_t>(int64_t{s.hx} * side / 1000);
        }
    }
    // The world's places come from the state alone (the block or the column, the stack's real heading).
    std::vector<std::pair<int32_t, int32_t>> slots;
    layout(s, slots, !s.path.empty() && !s.boat);
    int32_t command = 0;
    for (const Army& a : s.armies) command += Campaign::command(a);
    size_t k = 0;
    // A garrison at rest in its town is inside its walls: the rings round its keep, or none.
    std::vector<const Rampart*> walls;
    if (s.path.empty() && !s.boat) {
        const int32_t town = cityAt(s.cx, s.cy);
        if (town >= 0 && cities_[static_cast<size_t>(town)].owner == s.owner) {
            const Vec3 heart = continent_.cities()[static_cast<size_t>(town)].pos - origin;
            for (const Rampart& rp : region_->ramparts())
                if ((rp.centre - heart).flat().length() < Fixed::fromInt(20)) walls.push_back(&rp);
        }
    }
    std::vector<Vec3> taken;
    // Where a man may stand: on open ground (not in water, a house, a wall or a tree), inside the walls
    // if it is a garrison's, and clear of the others. His slot if it fits, else the nearest place that does.
    const auto fits = [&](const Vec3& p) {
        if (!world_->standable(p)) return false;
        if (!walls.empty()) {
            bool inside = false;
            for (const Rampart* rp : walls) inside = inside || Gate::inside(*rp, p);
            if (!inside) return false;
        }
        for (const Vec3& q : taken)
            if ((p - q).flat().lengthSq() < Fixed::fromRatio(9, 4)) return false;
        return true;
    };
    // (Then on the millimetre the campaign keeps him at, so that a save, which keeps no finer, stands him exactly where he is.)
    const auto place = [&](Vec3 p) {
        const Vec3 q = nearestFit(p, fits);
        return Vec3{fromMm(toMm(q.x + origin.x)) - origin.x, fromMm(toMm(q.y + origin.y)) - origin.y, q.z};
    };
    for (Army& a : s.armies) {
        int32_t arms = 0;
        for (uint8_t art : a.artifacts) arms += artifacts()[art].battle;
        const UnitStats stats = unitStats(a.type);
        for (Man& m : a.men) {
            if (!m.alive()) continue;
            const std::pair<int32_t, int32_t> slot = slots[k++];
            if (m.unit) continue;  // (already in the world)
            Vec3 at;
            int32_t fx = s.hx, fy = s.hy;
            if (m.placed) {
                // Where a save or a landing says he stands: exactly there.
                at = Vec3{fromMm(m.ax) - origin.x, fromMm(m.ay) - origin.y, Fixed::zero()};
                fx = m.fx, fy = m.fy;
            } else {
                int32_t dx, dy;
                slotToWorld(slot, s.hx, s.hy, dx, dy);
                at = place(Vec3{fromMm(int64_t{bx} + dx) - origin.x, fromMm(int64_t{by} + dy) - origin.y, Fixed::zero()});
                taken.push_back(at);
            }
            m.unit = world_->spawnUnit(s.owner, at, Vec3{fromMm(fx), fromMm(fy), Fixed::zero()}, stats);
            m.rank = std::max(m.rank, a.rank);
            world_->setVeterancy(m.unit, m.rank, a.blessings + arms + command);
            world_->restoreUnit(m.unit, m.hp < 0 ? INT32_MAX : m.hp, m.kills, m.morale);
            m.ax = m.bx = toMm(at.x + origin.x);
            m.ay = m.by = toMm(at.y + origin.y);
            m.fx = fx;
            m.fy = fy;
            m.placed = false;
            m.lastCool = m.lastReload = 0;
            // (Kept on the man as well: it is what a save writes, and release brings it up to date from the unit.)
            if (!m.queue.empty()) world_->restoreQueue(m.unit, m.queue);
        }
        a.seated = true;
    }
    int64_t sx = 0, sy = 0, n = 0;
    for (const Army& a : s.armies)
        for (const Man& m : a.men)
            if (m.alive() && m.unit) sx += m.ax, sy += m.ay, ++n;
    s.px = n > 0 ? static_cast<int32_t>(sx / n) : bx;
    s.py = n > 0 ? static_cast<int32_t>(sy / n) : by;
    s.orderCell = -1;
    s.lx = s.px;
    s.ly = s.py;
    s.lt = tick_;
    s.forceUntil = 0;
}

void Campaign::release(Army& a) {
    for (Man& m : a.men) {
        if (!m.unit) continue;
        const Unit* u = world_ ? world_->findUnit(m.unit) : nullptr;
        if (m.alive() && u && u->alive()) {
            m.hp = u->hp == u->stats.maxHp ? -1 : u->hp;
            m.kills = u->kills;
            m.morale = u->morale;
            m.rank = u->rank;
            m.ax = toMm(u->pos.x + region_->origin().x);
            m.ay = toMm(u->pos.y + region_->origin().y);
            m.fx = toMm(u->facing.x);
            m.fy = toMm(u->facing.y);
            m.placed = true;
            m.queue.clear();
            for (const Command& c : u->queue)
                if (c.type != Command::Type::AttackUnit) m.queue.push_back(c);
        } else if (m.alive()) {
            m.hp = 0;
        }
        if (u) world_->removeUnit(m.unit);
        m.unit = 0;
        m.struckTick = m.hurtTick = m.fightTick = 0;
    }
    a.seated = false;
}

void Campaign::seatNew() {
    bool any = false;
    for (const Stack& s : stacks_)
        if (!s.boat)
            for (const Army& a : s.armies) any = any || !a.seated;
    if (!any) return;
    ensureWorld();
    for (Stack& s : stacks_) {
        if (s.boat) continue;
        bool unseated = false;
        for (const Army& a : s.armies) unseated = unseated || !a.seated;
        if (unseated) seat(s);
    }
}

void Campaign::reseatAll() {
    ensureWorld();
    for (Stack& s : stacks_)
        if (!s.boat) syncMen(s);
    for (Stack& s : stacks_) {
        s.orderCell = -1;
        s.commanded = false;
        for (Army& a : s.armies) release(a);
    }
    freshWorld();
}

void Campaign::freshWorld() {
    ensureWorld();
    corpses_.clear();
    endFight(seed_ ^ tick_);  // the air cleared, and the world as it is at the start of any (the same on every machine)
    seatNew();
}

void Campaign::syncMen(Stack& s) {
    for (Army& a : s.armies) {
        for (Man& m : a.men) {
            if (!m.unit) continue;
            const Unit* u = world_->findUnit(m.unit);
            if (!u) {
                if (m.alive()) m.hp = 0;
                continue;
            }
            m.bx = m.ax;
            m.by = m.ay;
            m.ax = toMm(u->pos.x + region_->origin().x);
            m.ay = toMm(u->pos.y + region_->origin().y);
            m.fx = toMm(u->facing.x);
            m.fy = toMm(u->facing.y);
            if (u->kills != m.kills) {
                a.kills += std::max(0, u->kills - m.kills);
                m.kills = u->kills;
                promote(a);
            }
            if (!m.alive()) continue;
            if (!u->alive()) {
                m.hp = 0;
                a.soldiers = std::max(0, a.soldiers - 1);
                corpses_.push_back({tick_ + kCorpseTicks, m.unit});
                m.fightTick = tick_;
                continue;
            }
            const int32_t before = m.hp < 0 ? u->stats.maxHp : m.hp;
            if (u->hp < before) m.hurtTick = tick_;
            m.hp = u->hp;
            if (u->meleeCooldown > m.lastCool || u->reloadRemaining > m.lastReload) m.struckTick = tick_;
            m.lastCool = u->meleeCooldown;
            m.lastReload = u->reloadRemaining;
            m.morale = u->morale;
            if (u->engaged || m.struckTick == tick_ || m.hurtTick == tick_) m.fightTick = tick_;
        }
    }
}

// ---------------------------------------------------------------- orders

bool Campaign::underOrders(const Stack& s) const {
    for (const Army& a : s.armies)
        for (const Man& m : a.men) {
            const Unit* u = m.alive() && m.unit && world_ ? world_->findUnit(m.unit) : nullptr;
            if (u && u->alive() && (u->hasMoveTarget || u->hasAttackTarget || u->targetUnit != kNoEntity)) return true;
        }
    return false;
}

// ---------------------------------------------------------------- stepping

void Campaign::steer(Stack& s, std::vector<Command>& commands, bool think) {
    if (think) {
        // The middle of its standing men: where the stack is.
        int64_t sx = 0, sy = 0, fx = 0, fy = 0, n = 0;
        for (const Army& a : s.armies)
            for (const Man& m : a.men)
                if (m.alive() && m.unit) sx += m.ax, sy += m.ay, fx += m.fx, fy += m.fy, ++n;
        if (n > 0) {
            s.px = static_cast<int32_t>(sx / n);
            s.py = static_cast<int32_t>(sy / n);
            unitOf(fx, fy, s.hx, s.hy);
            // A stack is where its men are: the cell they are in while it marches or is commanded, and once they have
            // got well clear of the one it stood in (chasing a foe off, or put out of a town) the cell they stand in now.
            const int32_t ccx = std::clamp(cellOf(s.px / 1000), 0, nx_ - 1), ccy = std::clamp(cellOf(s.py / 1000), 0, ny_ - 1);
            if (!s.path.empty() || s.commanded) {
                s.cx = ccx;
                s.cy = ccy;
            } else if (ccx != s.cx || ccy != s.cy) {
                int32_t ax, ay;
                anchorOf(s.cx, s.cy, ax, ay);
                const int64_t ex = s.px - ax, ey = s.py - ay;
                if (ex * ex + ey * ey > int64_t{60000} * 60000) s.cx = ccx, s.cy = ccy;
            }
        }
    }
    if (s.commanded) return;  // (its men stand to the player's orders)
    if (s.path.empty()) {
        if (s.orderCell >= 0) {
            s.orderCell = -1;
            for (const Army& a : s.armies)
                for (const Man& m : a.men)
                    if (m.alive() && m.unit) commands.push_back({s.owner, m.unit, Command::Type::Stop, {}});
        }
        return;
    }
    int32_t tx, ty;
    anchorOf(s.path.front().first, s.path.front().second, tx, ty);
    if (think) {
        const int64_t dx = tx - s.px, dy = ty - s.py;
        // Reached when the middle of the men is at the anchor, or, on the way (not at the end of it), most of them are
        // about it (the anchor may be a tree or a wall's foot: they get as near as they can, and the column does not
        // wait on the one that cannot).
        int64_t about = 0, living = 0;
        for (const Army& a : s.armies)
            for (const Man& m : a.men)
                if (m.alive() && m.unit) {
                    ++living;
                    const int64_t ex = m.ax - tx, ey = m.ay - ty;
                    about += ex * ex + ey * ey < kAboutMm * kAboutMm;
                }
        if (dx * dx + dy * dy < kArriveMm * kArriveMm || (s.path.size() > 1 && about * 10 >= living * 6)) {
            // On to the next cell of the route.
            s.cx = s.path.front().first;
            s.cy = s.path.front().second;
            s.path.erase(s.path.begin());
            s.orderCell = -1;
            pickUp(s);
            if (s.path.empty()) return;
            if (navigable(s.path.front().first, s.path.front().second) && !passable(s, s.path.front().first, s.path.front().second)) {
                boarding_.push_back(s.id);  // at the quay: the boat takes them (after this step, stepWorld)
                return;
            }
            anchorOf(s.path.front().first, s.path.front().second, tx, ty);
        }
    }
    // Held up for ten seconds, with nobody striking or struck: a foe it cannot get at (behind a wall, in a
    // wood), so it goes deaf to him for ten seconds, as in a force march, and pulls itself free.
    if (think) {
        const int64_t dx = s.px - s.lx, dy = s.py - s.ly;
        bool fighting = false;
        for (const Army& a : s.armies)
            for (const Man& m : a.men) fighting = fighting || (m.alive() && m.unit && tick_ - m.fightTick < 5 * kTicksPerSecond);
        if (dx * dx + dy * dy >= 4000 * 4000 || fighting) {
            s.lx = s.px;
            s.ly = s.py;
            s.lt = tick_;
        } else if (tick_ - s.lt >= 10 * kTicksPerSecond && tick_ >= s.forceUntil) {
            if (s.forceUntil != 0 && tick_ < s.forceUntil + 30 * kTicksPerSecond) {
                // Held up again: the way on the campaign grid is not one the ground allows (a cell inside a wall, a
                // drowned one), so the cell is given up and the next is the way.
                s.cx = s.path.front().first;
                s.cy = s.path.front().second;
                s.path.erase(s.path.begin());
                pickUp(s);
                if (s.path.empty()) {
                    s.orderCell = -1;
                    return;
                }
            } else {
                s.forceUntil = tick_ + 10 * kTicksPerSecond;
            }
            s.lt = tick_;
            s.orderCell = -1;
        }
    }
    anchorOf(s.path.front().first, s.path.front().second, tx, ty);
    const int32_t cell = s.path.front().second * nx_ + s.path.front().first;
    if (s.orderCell == cell) return;
    s.orderCell = cell;
    // March on: every man to his place in the column at the cell's anchor, at the pace of the slowest.
    int32_t ux = s.hx, uy = s.hy;
    unitOf(int64_t{tx} - s.px, int64_t{ty} - s.py, ux, uy);
    Fixed pace = Fixed::fromInt(100);
    for (const Army& a : s.armies) pace = min(pace, unitStats(a.type).moveSpeed);
    std::vector<std::pair<int32_t, int32_t>> slots;
    layout(s, slots, s.path.size() > 1);  // (a column on the way, a block at the end of it)
    const Vec3 origin = region_->origin();
    const Vec3 from{fromMm(s.px) - origin.x, fromMm(s.py) - origin.y, Fixed::zero()}, to{fromMm(tx) - origin.x, fromMm(ty) - origin.y, Fixed::zero()};
    const Vec3 face{fromMm(ux), fromMm(uy), Fixed::zero()};
    size_t k = 0;
    for (const Army& a : s.armies)
        for (const Man& m : a.men) {
            if (!m.alive()) continue;
            const std::pair<int32_t, int32_t> slot = slots[k++];
            if (!m.unit) continue;
            int32_t dx, dy;
            slotToWorld(slot, ux, uy, dx, dy);
            Command c;
            c.player = s.owner;
            c.unit = m.unit;
            c.type = Command::Type::Move;
            // (his slot, or the nearest open ground to it: a slot in a tree or a wall's foot is no place to send a man)
            c.target = nearestFit(Vec3{fromMm(int64_t{tx} + dx) - origin.x, fromMm(int64_t{ty} + dy) - origin.y, Fixed::zero()},
                                  [&](const Vec3& q) { return world_->standable(q); });
            c.speed = pace;
            c.march = true;
            c.force = tick_ < s.forceUntil;
            c.marchFrom = from;
            c.marchTo = to;
            c.face = face;
            commands.push_back(c);
        }
}

// Men standing, with the battle's own habits: foot go at the nearest foe near
// their post, missile troops close to a good bowshot and shoot on their own;
// a garrison holds its post and meets whoever comes near it.
void Campaign::fightAi(Stack& s, std::vector<Command>& commands) {
    const World& w = *world_;
    const Vec3 origin = region_->origin();
    int32_t ax, ay;
    anchorOf(s.cx, s.cy, ax, ay);
    // The foes are the awake units of other sides, standing and on their feet: a grid of 16 m cells of every such unit,
    // built once a think. The nearest to a man is found in the few cells round him, never by walking them all.
    constexpr int32_t kCellM = 16;
    const auto cellOf = [&](Fixed v) { return std::clamp<int32_t>(static_cast<int32_t>(v.floorToInt() / kCellM), 0, foeCells_ - 1); };
    if (!foeGridBuilt_) {
        foeGridBuilt_ = true;
        foeCells_ = static_cast<int32_t>(region_->terrain().size().floorToInt() / kCellM) + 2;
        const auto slot = [&](const Unit& u) { return static_cast<size_t>(cellOf(u.pos.y)) * foeCells_ + cellOf(u.pos.x); };
        const auto isFoe = [](const Unit& u) { return u.alive() && !u.fleeing() && !u.inAir(); };
        foeStart_.assign(static_cast<size_t>(foeCells_) * foeCells_ + 1, 0);
        for (const Unit& u : w.units())
            if (isFoe(u)) ++foeStart_[slot(u) + 1];
        for (size_t c = 0; c + 1 < foeStart_.size(); ++c) foeStart_[c + 1] += foeStart_[c];
        foeItems_.assign(foeStart_.back(), 0);
        std::vector<uint32_t> fill(foeStart_.begin(), foeStart_.end() - 1);
        for (uint32_t i = 0; i < w.units().size(); ++i)
            if (isFoe(w.units()[i])) foeItems_[fill[slot(w.units()[i])]++] = i;
    }
    for (const Army& a : s.armies) {
        const UnitStats stats = unitStats(a.type);
        if (stats.healing.amount > 0) continue;  // healers tend, and follow
        const bool missiles = stats.ranged.muzzleSpeed > Fixed::zero();
        for (const Man& m : a.men) {
            if (!m.alive() || !m.unit) continue;
            const Unit* u = w.findUnit(m.unit);
            if (!u || !u->alive() || u->routed || u->targetUnit != kNoEntity || u->foe != kNoEntity) continue;
            if (w.gateKeeper(*u)) continue;  // a garrison keeps its gate: its stations are the World's (keepGates), not a chase outside the walls
            // The nearest foe (of those as near, the first in the world's order). Only one within hold of the post
            // matters, and the man is not far from it: ring after ring of cells to a little past that is enough (a
            // nearest beyond them is beyond the hold, and he does nothing either way).
            const int64_t offMm = std::abs(int64_t{toMm(u->pos.x + origin.x)} - ax) + std::abs(int64_t{toMm(u->pos.y + origin.y)} - ay);
            const int32_t rings = static_cast<int32_t>((kHoldMm + offMm + 4000) / (kCellM * 1000)) + 2;
            const Unit* best = nullptr;
            int64_t bestRaw = INT64_MAX;
            uint32_t bestIndex = 0;
            const int32_t cx = cellOf(u->pos.x), cy = cellOf(u->pos.y);
            for (int32_t r = 0; r <= rings; ++r) {
                for (int32_t y = std::max(0, cy - r); y <= std::min(foeCells_ - 1, cy + r); ++y) {
                    // This ring only: the whole of its first and last rows, the two ends of the rest.
                    const int32_t step = (y == cy - r || y == cy + r) ? 1 : 2 * r;
                    for (int32_t x = cx - r; x <= cx + r; x += step) {
                        if (x < 0 || x >= foeCells_) continue;
                        const size_t c = static_cast<size_t>(y) * foeCells_ + x;
                        for (uint32_t k = foeStart_[c]; k < foeStart_[c + 1]; ++k) {
                            const Unit& f = w.units()[foeItems_[k]];
                            if (f.owner == s.owner) continue;
                            const int64_t d = (f.pos - u->pos).flat().lengthSq().raw;
                            if (d < bestRaw || (d == bestRaw && foeItems_[k] < bestIndex)) {
                                bestRaw = d;
                                best = &f;
                                bestIndex = foeItems_[k];
                            }
                        }
                    }
                }
                // Nothing in a ring beyond this one is nearer than its inner edge.
                if (best && bestRaw + 4 < int64_t{r} * r * kCellM * kCellM * Fixed::kOneRaw) break;
            }
            if (!best) continue;
            const int64_t dist = toMm(sqrt(Fixed::fromRaw(bestRaw)));
            const int64_t px = toMm(best->pos.x + origin.x) - ax, py = toMm(best->pos.y + origin.y) - ay;
            if (px * px + py * py > kHoldMm * kHoldMm) continue;
            Command c;
            c.player = s.owner;
            c.unit = m.unit;
            if (missiles) {
                // A good bowshot off, then they shoot on their own.
                const int64_t keep = toMm(stats.ranged.autoRange) * 4 / 5;
                if (dist <= keep) continue;
                const Vec3 d = (best->pos - u->pos).flat();
                const Fixed len = d.length();
                c.type = Command::Type::Move;
                c.target = u->pos + d * ((len - fromMm(keep)) / len);
                c.speed = Fixed::zero();
            } else {
                c.type = Command::Type::AttackUnit;
                c.targetUnit = best->id;
            }
            commands.push_back(c);
        }
    }
}

uint32_t Campaign::stackOfUnit(uint32_t unit) const {
    for (const Stack& s : stacks_)
        for (const Army& a : s.armies)
            for (const Man& m : a.men)
                if (m.unit == unit) return s.id;
    return 0;
}

void Campaign::endFight(uint64_t seed) {
    world_->clearFight(seed);
    if (clearedHook_) clearedHook_();
}

void Campaign::stepWorld() {
    seatNew();  // the men raised, hired, put ashore or loaded since the last step
    if (!world_) return;
    while (!corpses_.empty() && corpses_.front().first <= tick_) {
        const uint32_t gone = corpses_.front().second;
        world_->removeUnit(gone);
        corpses_.erase(corpses_.begin());
        // His place in the world is empty and goes to the next man to come: he must not keep it as his (the soldier
        // list and the stack of a unit would both take the new man for him).
        for (Stack& s : stacks_)
            for (Army& a : s.armies)
                for (Man& m : a.men)
                    if (m.unit == gone) m.unit = 0;
    }
    const bool think = tick_ % kThinkTicks == 0;
    foeGridBuilt_ = false;
    commands_.clear();
    boarding_.clear();
    for (Stack& s : stacks_) {
        if (s.boat || s.armies.empty()) continue;
        steer(s, commands_, think);
        // Marching, men fight with the battle's initiative (whoever strikes them or crosses their path); standing, the
        // computer lords' men and the player's alike meet whoever comes near their post (fightAi).
        if (think && s.path.empty() && !s.commanded) fightAi(s, commands_);
    }
    // The player's orders to men of the world go in after the stacks' own, whose men they are given to (commandMen).
    commands_.insert(commands_.end(), playerCommands_.begin(), playerCommands_.end());
    playerCommands_.clear();
    if (beforeHook_) beforeHook_();
    world_->step(commands_);
    if (stepHook_) stepHook_();
    for (Stack& s : stacks_)
        if (!s.boat) syncMen(s);
    // The armies that have fallen to the last man are gone, and heroes drop what they carried.
    for (Stack& s : stacks_)
        if (!s.boat) bury(s, s.cx, s.cy);
    const size_t before = stacks_.size();
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.armies.empty(); }), stacks_.end());
    if (stacks_.size() != before) syncBoats();
    // Those at a quay with a boat to take them go aboard (it may split the stack: after the loop over them).
    for (uint32_t id : boarding_)
        for (size_t i = 0; i < stacks_.size(); ++i)
            if (stacks_[i].id == id && !stacks_[i].path.empty()) boardIfSailing(i);
}

}  // namespace illyria::sim
