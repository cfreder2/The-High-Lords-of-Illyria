// The men on the land (docs/ONE_WORLD.md, "Armies on the land" and "Fights").
//
// Every squad is its real men, each with a place on the ground. The campaign
// holds one sim::World on the ground of the whole island, with all eighty
// castles in it, and puts into it the men who are in a fight:
//
// - Asleep, a stack is one body. It marches its route at its pace on the
//   campaign grid (campaign.cpp moveStacks), and every man stands in his slot of
//   its formation, or is walking back to it from where a fight left him. This
//   costs nothing a tick and is all that most of the island is doing.
// - Awake, its men are units of the world, stepped with the battle's own rules:
//   orders, morale, initiative, formations, walls, gates, arrows. A stack wakes
//   when an enemy stack comes within kContactRange of it and either is on the
//   move, and sleeps again when none has been that near for kCalmTicks, each
//   man where the fight left him (and eases back to his slot).
//
// Which stacks are awake depends only on where everyone stands, never on the
// camera, so every machine sees the same fights. Nothing is settled by dice:
// men die, towns fall by banners raised over them (campaign_capture.cpp).
#include <algorithm>
#include <cmath>
#include <map>

#include "illyria/sim/campaign.h"

namespace illyria::sim {

namespace {

constexpr int32_t kIslandCells = 900;  // the island's ground, 3.6 km of 4 m cells (the map is 2.8 x 3.6 km)
constexpr uint32_t kCorpseTicks = 600 * kTicksPerSecond;  // the dead lie as long as the fight lasts, as in the battle (ten minutes at most)
constexpr int64_t kArriveMm = 12000;  // marching, a cell is reached within this of its anchor
constexpr int64_t kHoldMm = 45000;  // men standing meet whoever gets this near their post
constexpr int32_t kAbreast = 4;
constexpr int32_t kSideBySideMm = 11000;
constexpr int64_t kTownReachMm = 95000;  // how near a town's walls an enemy must come to wake those resting in it

int64_t isqrt(int64_t n) {
    if (n <= 0) return 0;
    int64_t x = static_cast<int64_t>(std::sqrt(static_cast<double>(n)));
    while (x * x > n) --x;
    while ((x + 1) * (x + 1) <= n) ++x;
    return x;
}
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
    if (s.awake) {
        x = s.px;
        y = s.py;
        return;
    }
    if (s.boat) {
        x = centre(s.cx) * 1000;
        y = centre(s.cy) * 1000;
    } else {
        anchorOf(s.cx, s.cy, x, y);
    }
    if (s.path.empty()) return;
    const int32_t leg = legTime(s);
    if (leg <= 0 || leg == kClosed) return;
    int32_t tx, ty;
    if (s.boat) {
        tx = centre(s.path.front().first) * 1000;
        ty = centre(s.path.front().second) * 1000;
    } else {
        anchorOf(s.path.front().first, s.path.front().second, tx, ty);
    }
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

constexpr float kGlideMetresPerSecond = 3.0f;
constexpr float kFormMetresPerSecond = 5.0f;

void Campaign::formation(const Stack& s, std::vector<std::pair<int32_t, int32_t>>& slots) const {
    const bool marching = !s.path.empty() && !s.boat;
    if (s.form <= 0 || s.form >= 1000) {
        layout(s, slots, s.form >= 1000 || (s.form > 0 && marching));
        return;
    }
    std::vector<std::pair<int32_t, int32_t>> block, column;
    layout(s, block, false);
    layout(s, column, true);
    slots.resize(block.size());
    for (size_t i = 0; i < block.size(); ++i) {
        slots[i].first = static_cast<int32_t>(block[i].first + int64_t{column[i].first - block[i].first} * s.form / 1000);
        slots[i].second = static_cast<int32_t>(block[i].second + int64_t{column[i].second - block[i].second} * s.form / 1000);
    }
}

void Campaign::soldiers(int32_t ahead, std::vector<Soldier>& out) const {
    out.clear();
    std::map<int32_t, int32_t> standing;  // stationary stacks to a cell, to set them side by side
    std::vector<std::pair<int32_t, int32_t>> slots;
    const float alpha = std::clamp(static_cast<float>(ahead) / kTimeScale, 0.0f, 1.0f);
    for (const Stack& s : stacks_) {
        if (s.armies.empty()) continue;
        if (s.awake) {
            for (const Army& a : s.armies)
                for (const Man& m : a.men) {
                    if (!m.unit) continue;
                    Soldier d;
                    d.id = m.id;
                    d.stack = s.id;
                    d.army = a.id;
                    d.owner = s.owner;
                    d.type = a.type;
                    d.awake = true;
                    d.alive = m.hp != 0;
                    if (!d.alive && (!world_ || !world_->findUnit(m.unit))) continue;
                    d.x = (static_cast<float>(m.bx) + static_cast<float>(m.ax - m.bx) * alpha + static_cast<float>(m.gx)) / 1000.0f;
                    d.y = (static_cast<float>(m.by) + static_cast<float>(m.ay - m.by) * alpha + static_cast<float>(m.gy)) / 1000.0f;
                    d.yaw = std::atan2(static_cast<float>(m.fy), static_cast<float>(m.fx));
                    d.speed = d.alive && (m.ax != m.bx || m.ay != m.by)
                                  ? std::hypot(static_cast<float>(m.ax - m.bx), static_cast<float>(m.ay - m.by)) / 1000.0f * kTicksPerSecond
                                  : 0.0f;
                    if (m.gx != 0 || m.gy != 0) d.speed = std::max(d.speed, kGlideMetresPerSecond);  // (he is walking to his place)
                    d.aloft = d.alive && flies(a.type) && world_ && world_->findUnit(m.unit) && world_->findUnit(m.unit)->flight == Unit::Flight::Flying;
                    d.unit = world_ ? world_->findUnit(m.unit) : nullptr;
                    d.unitId = m.unit;
                    d.struckTick = m.struckTick;
                    d.hurtTick = m.hurtTick;
                    d.fightTick = m.fightTick;
                    out.push_back(d);
                }
            continue;
        }
        int32_t bx, by;
        bodyOf(s, ahead, bx, by);
        if (s.path.empty() && !s.boat) {
            const int32_t rank = standing[s.cy * nx_ + s.cx]++;
            if (rank > 0) {
                const int32_t side = (rank % 2 ? 1 : -1) * ((rank + 1) / 2) * kSideBySideMm;
                bx += static_cast<int32_t>(int64_t{s.hy} * side / 1000);
                by -= static_cast<int32_t>(int64_t{s.hx} * side / 1000);
            }
        }
        formation(s, slots);
        float speed = 0;
        if (!s.path.empty() && !s.boat) {
            const int32_t leg = legTime(s);
            if (leg > 0 && leg != kClosed) {
                int32_t ax, ay, tx, ty;
                anchorOf(s.cx, s.cy, ax, ay);
                anchorOf(s.path.front().first, s.path.front().second, tx, ty);
                speed = static_cast<float>(std::hypot(static_cast<double>(tx - ax), static_cast<double>(ty - ay))) / 1000.0f /
                        (static_cast<float>(leg) / (kTimeScale * kTicksPerSecond));
            }
        }
        const bool aloft = speed > 0 && airborne(s);
        const float yaw = std::atan2(static_cast<float>(s.dhy), static_cast<float>(s.dhx));
        size_t k = 0;
        for (const Army& a : s.armies)
            for (const Man& m : a.men) {
                if (!m.alive()) continue;
                int32_t dx, dy;
                if (m.placed) {
                    dx = m.ox;
                    dy = m.oy;
                } else {
                    slotToWorld(slots[k], s.dhx, s.dhy, dx, dy);
                }
                ++k;
                Soldier d;
                d.id = m.id;
                d.stack = s.id;
                d.army = a.id;
                d.owner = s.owner;
                d.type = a.type;
                d.x = static_cast<float>(bx + dx) / 1000.0f;
                d.y = static_cast<float>(by + dy) / 1000.0f;
                d.yaw = yaw;
                d.speed = s.form > 0 && s.form < 1000 ? std::max(speed, kFormMetresPerSecond) : speed;  // (drawing out, or closing up)
                d.aloft = aloft;
                out.push_back(d);
            }
    }
}

int32_t Campaign::awakeStacks() const { return awakeStacks_; }

int32_t Campaign::awakeMen() const {
    int32_t n = 0;
    for (const Stack& s : stacks_)
        if (s.awake)
            for (const Army& a : s.armies) n += a.soldiers;
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
    world_->clearFight(seed_);
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

void Campaign::holdGates(int32_t city) {
    if (!world_) return;
    for (size_t g = 0; g < gateCity_.size(); ++g)
        if (gateCity_[g] == city) world_->setGateHolder(g, cities_[static_cast<size_t>(city)].owner);
}

void Campaign::wake(Stack& s) {
    ensureWorld();
    if (awakeStacks_ == 0) world_->clearFight(seed_ ^ tick_);  // every fight starts the same way on every machine
    const Vec3 origin = region_->origin();
    // Where it stands: its body, set beside others in the cell as the drawing sets it.
    int32_t bx, by;
    bodyOf(s, 0, bx, by);
    if (s.path.empty() && !s.boat) {
        int32_t rank = 0;
        for (const Stack& o : stacks_) {
            if (&o == &s) break;
            rank += !o.armies.empty() && o.path.empty() && !o.boat && !o.awake && o.cx == s.cx && o.cy == s.cy;
        }
        if (rank > 0) {
            const int32_t side = (rank % 2 ? 1 : -1) * ((rank + 1) / 2) * kSideBySideMm;
            bx += static_cast<int32_t>(int64_t{s.hy} * side / 1000);
            by -= static_cast<int32_t>(int64_t{s.hx} * side / 1000);
        }
    }
    s.px = bx;
    s.py = by;
    std::vector<std::pair<int32_t, int32_t>> slots;
    formation(s, slots);
    int32_t command = 0;
    for (const Army& a : s.armies) command += Campaign::command(a);
    size_t k = 0;
    const Vec3 face{fromMm(s.hx), fromMm(s.hy), Fixed::zero()};
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
    const auto place = [&](Vec3 p) {
        for (int ring = 0; ring <= 40 && !fits(p); ++ring) {
            const Vec3 base = p;
            for (int step = 0; step < std::max(1, 6 * ring); ++step) {
                // (a ring at a time, round the compass in sixths of a turn per metre of radius)
                static const int32_t kCos[24] = {1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259, 0, 259, 500, 707, 866, 966};
                static const int32_t kSin[24] = {0, 259, 500, 707, 866, 966, 1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259};
                const int32_t dir = step * 24 / std::max(1, 6 * ring);
                const Vec3 q{base.x + Fixed::fromRatio(kCos[dir] * ring, 1000), base.y + Fixed::fromRatio(kSin[dir] * ring, 1000), base.z};
                if (fits(q)) return q;
            }
        }
        return p;
    };
    for (Army& a : s.armies) {
        int32_t arms = 0;
        for (uint8_t art : a.artifacts) arms += artifacts()[art].battle;
        const UnitStats stats = unitStats(a.type);
        for (Man& m : a.men) {
            if (!m.alive()) continue;
            int32_t dx, dy;
            if (m.placed) {
                dx = m.ox;
                dy = m.oy;
            } else {
                slotToWorld(slots[k], s.dhx, s.dhy, dx, dy);
            }
            ++k;
            Vec3 at{fromMm(int64_t{bx} + dx) - origin.x, fromMm(int64_t{by} + dy) - origin.y, Fixed::zero()};
            at = place(at);
            taken.push_back(at);
            m.unit = world_->spawnUnit(s.owner, at, face, stats);
            m.rank = std::max(m.rank, a.rank);
            world_->setVeterancy(m.unit, m.rank, a.blessings + arms + command);
            world_->restoreUnit(m.unit, m.hp < 0 ? INT32_MAX : m.hp, m.kills, m.morale);
            m.ax = m.bx = toMm(at.x + origin.x);
            m.ay = m.by = toMm(at.y + origin.y);
            m.gx = static_cast<int32_t>(int64_t{bx} + dx - m.ax);  // (the squad's slot, if it did not fit, he walks from)
            m.gy = static_cast<int32_t>(int64_t{by} + dy - m.ay);
            m.fx = s.hx;
            m.fy = s.hy;
            m.lastCool = m.lastReload = 0;
        }
    }
    s.awake = true;
    s.calm = 0;
    s.orderCell = -1;
    s.lx = bx;
    s.ly = by;
    s.lt = tick_;
    s.forceUntil = 0;
    ++awakeStacks_;
}

void Campaign::sleep(Stack& s) {
    if (!s.awake) return;
    syncMen(s);
    // The cell the middle of them stands in is where the body picks up its route.
    int64_t sx = 0, sy = 0, n = 0;
    for (const Army& a : s.armies)
        for (const Man& m : a.men)
            if (m.alive() && m.unit) sx += m.ax, sy += m.ay, ++n;
    if (n > 0) {
        s.cx = std::clamp(cellOf(static_cast<int32_t>(sx / n / 1000)), 0, nx_ - 1);
        s.cy = std::clamp(cellOf(static_cast<int32_t>(sy / n / 1000)), 0, ny_ - 1);
    }
    s.progress = 0;
    s.awake = false;
    s.orderCell = -1;
    --awakeStacks_;
    // Where it was bound: the way on from where it stands now.
    if (!s.path.empty() && !s.boat) {
        const std::pair<int32_t, int32_t> goal = s.path.back();
        s.path.clear();
        move(s.id, goal.first, goal.second);
    }
    int32_t bx, by;
    bodyOf(s, 0, bx, by);
    if (s.path.empty() && !s.boat) {
        int32_t rank = 0;
        for (const Stack& o : stacks_) {
            if (&o == &s) break;
            rank += !o.armies.empty() && o.path.empty() && !o.boat && !o.awake && o.cx == s.cx && o.cy == s.cy;
        }
        if (rank > 0) {
            const int32_t side = (rank % 2 ? 1 : -1) * ((rank + 1) / 2) * kSideBySideMm;
            bx += static_cast<int32_t>(int64_t{s.hy} * side / 1000);
            by -= static_cast<int32_t>(int64_t{s.hx} * side / 1000);
        }
    }
    for (Army& a : s.armies) {
        int32_t alive = 0;
        for (Man& m : a.men) {
            if (!m.unit) continue;
            const Unit* u = world_ ? world_->findUnit(m.unit) : nullptr;
            if (m.alive() && u && u->alive()) {
                m.hp = u->hp == u->stats.maxHp ? -1 : u->hp;
                m.kills = u->kills;
                m.morale = u->morale;
                m.rank = u->rank;
                m.placed = true;
                m.ox = m.ax - bx;
                m.oy = m.ay - by;
                ++alive;
                world_->removeUnit(m.unit);
            } else if (m.alive()) {
                m.hp = 0;
            }
            m.unit = 0;
        }
        a.soldiers = alive;
    }
    if (awakeStacks_ == 0 && world_) {
        // The fight is over for good: its dead, its arrows and its dice are gone, and the world is as it was before any, but for the gates.
        corpses_.clear();
        world_->clearFight(seed_ ^ tick_);
    }
}

void Campaign::settleAll() {
    for (Stack& s : stacks_) settle(s);
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

// ---------------------------------------------------------------- contact

void Campaign::contactStep() {
    struct Body {
        size_t index;
        int32_t x, y, r;
        bool moving;
        bool resting;   // standing in a town of its own (or a garrison in its town)
        int32_t tx, ty;  // that town's courtyard
    };
    std::vector<Body> bodies;
    bodies.reserve(stacks_.size());
    for (size_t i = 0; i < stacks_.size(); ++i) {
        const Stack& s = stacks_[i];
        if (s.armies.empty() || s.boat) continue;  // (boats do not fight: they are still on the water)
        // Moving, or fighting (awake, and a man of it has struck or been struck in the last five seconds): the ones whose
        // being near an enemy is a reason to be in the world. Two stacks standing, neither fighting, are not.
        bool active = !s.path.empty();
        if (s.awake && !active)
            for (const Army& a : s.armies)
                for (const Man& m : a.men) active = active || (m.alive() && m.unit && tick_ - m.fightTick < 5 * kTicksPerSecond);
        Body b{i, 0, 0, 0, active, false, 0, 0};
        bodyOf(s, 0, b.x, b.y);
        if (s.path.empty() && terrain_[static_cast<size_t>(s.cy) * nx_ + s.cx] == Lay::City) {
            const int32_t town = cityAt(s.cx, s.cy);
            if (town >= 0 && cities_[static_cast<size_t>(town)].owner == s.owner) {
                b.resting = true;
                anchorOf(s.cx, s.cy, b.tx, b.ty);
            }
        }
        int32_t men = 0;
        for (const Army& a : s.armies) men += a.soldiers;
        b.r = 2000 + static_cast<int32_t>(isqrt(std::max(1, men))) * 1500;
        bodies.push_back(b);
    }
    // Neighbours by 200 m cells: the bodies sorted by cell, and each looks in the cells about its own.
    constexpr int32_t kCellMm = 200000;
    const int32_t gw = static_cast<int32_t>(Continent::kWidth * 1000 / kCellMm) + 2, gh = static_cast<int32_t>(Continent::kHeight * 1000 / kCellMm) + 2;
    const auto cellOf2 = [&](const Body& b) { return std::clamp(b.y / kCellMm, 0, gh - 1) * gw + std::clamp(b.x / kCellMm, 0, gw - 1); };
    std::vector<std::pair<int32_t, int32_t>> order;  // (cell, body)
    order.reserve(bodies.size());
    for (size_t k = 0; k < bodies.size(); ++k) order.push_back({cellOf2(bodies[k]), static_cast<int32_t>(k)});
    std::sort(order.begin(), order.end());
    std::vector<uint8_t> touched(stacks_.size(), 0);
    for (size_t k = 0; k < bodies.size(); ++k) {
        const Body& a = bodies[k];
        const int32_t gx = std::clamp(a.x / kCellMm, 0, gw - 1), gy = std::clamp(a.y / kCellMm, 0, gh - 1);
        for (int32_t y = std::max(0, gy - 1); y <= std::min(gh - 1, gy + 1); ++y) {
            // The three cells of a row are one run of the sorted bodies.
            const int32_t lo = y * gw + std::max(0, gx - 1), hi = y * gw + std::min(gw - 1, gx + 1);
            for (auto it = std::lower_bound(order.begin(), order.end(), std::make_pair(lo, int32_t{-1})); it != order.end() && it->first <= hi; ++it) {
                const int32_t j = it->second;
                if (static_cast<size_t>(j) <= k) continue;
                const Body& b = bodies[static_cast<size_t>(j)];
                if (!a.moving && !b.moving) continue;
                if (stacks_[a.index].owner == stacks_[b.index].owner) continue;
                const int64_t reach = int64_t{kContactRange} * 1000 + a.r + b.r;
                const int64_t dx = a.x - b.x, dy = a.y - b.y;
                if (dx * dx + dy * dy > reach * reach) continue;
                // An enemy at rest behind his own walls does not count (towns stand closer than that to one another):
                // only whoever comes up to them does.
                const auto atWalls = [](const Body& rest, const Body& other) {
                    const int64_t ex = other.x - rest.tx, ey = other.y - rest.ty, near = kTownReachMm + other.r;
                    return ex * ex + ey * ey <= near * near;
                };
                if ((a.resting && !atWalls(a, b)) || (b.resting && !atWalls(b, a))) continue;
                touched[a.index] = touched[b.index] = 1;
            }
        }
    }
    for (const Body& b : bodies) {
        Stack& s = stacks_[b.index];
        if (touched[b.index]) {
            s.calm = 0;
            if (!s.awake) wake(s);
        } else if (s.awake) {
            s.calm += kContactTicks;
            if (s.calm >= kCalmTicks) sleep(s);
        }
    }
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
            if (!s.path.empty()) {  // (a stack standing still keeps the place it holds)
                s.cx = std::clamp(cellOf(s.px / 1000), 0, nx_ - 1);
                s.cy = std::clamp(cellOf(s.py / 1000), 0, ny_ - 1);
            }
        }
    }
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
        if (dx * dx + dy * dy < kArriveMm * kArriveMm) {
            // On to the next cell of the route.
            s.cx = s.path.front().first;
            s.cy = s.path.front().second;
            s.path.erase(s.path.begin());
            s.orderCell = -1;
            pickUp(s);
            if (s.path.empty()) return;
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
    layout(s, slots, !s.path.empty() && !s.boat);
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
            c.target = Vec3{fromMm(int64_t{tx} + dx) - origin.x, fromMm(int64_t{ty} + dy) - origin.y, Fixed::zero()};
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
    // The foes, once: awake units of other sides, standing and on their feet.
    std::vector<const Unit*> foes;
    for (const Unit& u : w.units())
        if (u.alive() && u.owner != s.owner && !u.routed && !u.inAir()) foes.push_back(&u);
    if (foes.empty()) return;
    for (const Army& a : s.armies) {
        const UnitStats stats = unitStats(a.type);
        if (stats.healing.amount > 0) continue;  // healers tend, and follow
        const bool missiles = stats.ranged.muzzleSpeed > Fixed::zero();
        for (const Man& m : a.men) {
            if (!m.alive() || !m.unit) continue;
            const Unit* u = w.findUnit(m.unit);
            if (!u || !u->alive() || u->routed || u->targetUnit != kNoEntity || u->foe != kNoEntity) continue;
            const Unit* best = nullptr;
            int64_t bestRaw = INT64_MAX;
            for (const Unit* f : foes) {
                const int64_t d = (f->pos - u->pos).flat().lengthSq().raw;
                if (d < bestRaw) {
                    bestRaw = d;
                    best = f;
                }
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

void Campaign::takeImpacts(std::vector<ImpactEvent>& out) {
    out.clear();
    out.swap(impactLog_);
}

void Campaign::stepWorld() {
    awakeStacks_ = 0;  // (counted afresh: stacks come and go in many places)
    for (const Stack& s : stacks_) awakeStacks_ += s.awake && !s.armies.empty();
    while (!corpses_.empty() && corpses_.front().first <= tick_ && awakeStacks_ > 0) {
        if (world_) world_->removeUnit(corpses_.front().second);
        corpses_.erase(corpses_.begin());
    }
    if (awakeStacks_ == 0) return;
    const bool think = tick_ % kContactTicks == 0;
    commands_.clear();
    for (Stack& s : stacks_) {
        if (!s.awake || s.armies.empty()) continue;
        steer(s, commands_, think);
        // Marching, men fight with the battle's initiative (whoever strikes them or crosses their path); standing, the
        // computer lords' men and the player's alike meet whoever comes near their post (fightAi).
        if (think && s.path.empty()) fightAi(s, commands_);
    }
    world_->step(commands_);
    if (stepHook_) stepHook_(*world_);
    if (impactLog_.size() < 4000) impactLog_.insert(impactLog_.end(), world_->impacts().begin(), world_->impacts().end());
    for (Stack& s : stacks_)
        if (s.awake) {
            syncMen(s);
            // Men who woke off their slots walk the last of the way to their places.
            for (Army& a : s.armies)
                for (Man& m : a.men) {
                    if (m.gx == 0 && m.gy == 0) continue;
                    const double len = std::hypot(static_cast<double>(m.gx), static_cast<double>(m.gy));
                    const double step = kGlideMetresPerSecond * 1000.0 / kTicksPerSecond;
                    if (len <= step) m.gx = m.gy = 0;
                    else m.gx -= static_cast<int32_t>(m.gx * step / len), m.gy -= static_cast<int32_t>(m.gy * step / len);
                }
        }
    // The armies that have fallen to the last man are gone, and heroes drop what they carried.
    bool gone = false;
    for (Stack& s : stacks_) {
        if (!s.awake) continue;
        const size_t before = s.armies.size();
        bury(s, s.cx, s.cy);
        gone = gone || s.armies.size() != before;
    }
    (void)gone;
    const size_t before = stacks_.size();
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(),
                                 [&](const Stack& s) {
                                     if (!s.armies.empty()) return false;
                                     if (s.awake) --awakeStacks_;
                                     return true;
                                 }),
                  stacks_.end());
    if (stacks_.size() != before) syncBoats();
    if (awakeStacks_ == 0) {
        // The last of them fell: the fight is over (as in sleep, for the world's sake).
        corpses_.clear();
        world_->clearFight(seed_ ^ tick_);
    }
}

}  // namespace illyria::sim
