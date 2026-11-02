#include "illyria/game/campaign_soldiers.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

namespace {
using C = sim::Campaign;
constexpr float kPi = 3.14159265f;
// A squad stands this many abreast at most, with its ranks a little further
// apart than its files; squads follow one another a few metres apart.
constexpr int kAbreast = 4;
constexpr float kRankDepth = 1.25f, kSquadGap = 3.0f;
// Stacks sharing a town stand side by side this far apart.
constexpr float kSideBySide = 11.0f;
// A man is placed off his slot by up to this much, by his number, so that a
// rank is not ruled; far too little to show from the height of the points.
constexpr float kRagged = 0.22f;
// The heading eases round at about this rate (a share a second).
constexpr float kTurnRate = 3.0f;

float hash01(uint32_t v) {
    v *= 2654435761u;
    v ^= v >> 15;
    v *= 2246822519u;
    v ^= v >> 13;
    return static_cast<float>(v & 0xFFFFu) / 65535.0f;
}

// Where a cell is stood in: in a town, its courtyard (where the walls' defenders
// stand: the keep's own ground is built on), else the cell's middle.
void anchorOf(const C& c, int32_t cx, int32_t cy, float& x, float& y) {
    const int32_t town = c.cityAt(cx, cy);
    if (town >= 0) {
        const sim::Continent::City& city = c.continent().cities()[static_cast<size_t>(town)];
        const sim::Vec3& at = city.castle.courtyardRadius > sim::Fixed::zero() ? city.castle.courtyard : city.pos;
        x = static_cast<float>(at.x.toDouble());
        y = static_cast<float>(at.y.toDouble());
        return;
    }
    x = static_cast<float>(C::centre(cx));
    y = static_cast<float>(C::centre(cy));
}
}  // namespace

float soldierSpacing(sim::UnitType t) {
    return std::max(1.4f, 2.4f * static_cast<float>(sim::unitStats(t).radius.toDouble()));
}

StackPlace stackPlace(const C& c, const C::Stack& s, double ahead) {
    StackPlace p;
    anchorOf(c, s.cx, s.cy, p.x, p.y);
    if (s.path.empty()) return p;
    const int32_t leg = c.legTime(s);
    float tx, ty;
    anchorOf(c, s.path.front().first, s.path.front().second, tx, ty);
    const float dx = tx - p.x, dy = ty - p.y, len = std::hypot(dx, dy);
    const float t = leg > 0 && leg != C::kClosed ? std::clamp(static_cast<float>((s.progress + ahead) / leg), 0.0f, 1.0f) : 0.0f;
    p.x += dx * t;
    p.y += dy * t;
    if (len > 0.01f && leg > 0 && leg != C::kClosed) {
        p.dirX = dx / len;
        p.dirY = dy / len;
        // metres over the seconds the leg takes (hundredths of a tick; 30 ticks a second)
        p.speed = len / (static_cast<float>(leg) / (C::kTimeScale * C::kTicksPerSecond));
    }
    return p;
}

void layoutMapSoldiers(const C& c, double ahead, float dt, uint32_t selected, MapFacing& facing, std::vector<MapSoldier>& soldiers,
                       std::vector<StackShape>& shapes) {
    soldiers.clear();
    shapes.clear();
    // Which of several stacks standing in one cell this is, to set them side by side.
    std::map<int32_t, int> standing;
    std::map<uint32_t, float> keep;
    for (const C::Stack& s : c.stacks()) {
        if (s.armies.empty() || s.boat) continue;
        const StackPlace at = stackPlace(c, s, ahead);
        // Its heading: eased round to the way its leg runs, kept when it stops. A
        // stack that has not been seen faces the way the camera starts, so its
        // faces are shown (south).
        const auto known = facing.yaw.find(s.id);
        float yaw = known != facing.yaw.end() ? known->second : -kPi / 2;
        if (at.speed > 0) {
            const float want = std::atan2(at.dirY, at.dirX);
            const float turn = std::remainder(want - yaw, 2 * kPi);
            yaw += known == facing.yaw.end() ? turn : turn * std::min(1.0f, kTurnRate * dt);
        }
        keep[s.id] = yaw;
        const float fx = std::cos(yaw), fy = std::sin(yaw), rx = fy, ry = -fx;  // forward, and to the right of it
        float cx = at.x, cy = at.y;
        if (s.path.empty()) {
            const int rank = standing[s.cy * c.width() + s.cx]++;
            if (rank > 0) {
                const float side = (rank % 2 ? 1.0f : -1.0f) * ((rank + 1) / 2) * kSideBySide;
                cx += rx * side;
                cy += ry * side;
            }
        }
        // The column: each squad a block of ranks and files, the first army in front.
        struct Block {
            int cols, rows, n;
            float spacing, depth;
        };
        std::vector<Block> blocks;
        float length = 0, width = 0;
        for (const sim::Army& a : s.armies) {
            Block b;
            b.n = std::max(1, a.soldiers);
            b.spacing = soldierSpacing(a.type);
            b.cols = std::min(b.n, kAbreast);
            b.rows = (b.n + b.cols - 1) / b.cols;
            b.depth = (b.rows - 1) * b.spacing * kRankDepth;
            blocks.push_back(b);
            length += b.depth + kSquadGap;
            width = std::max(width, (b.cols - 1) * b.spacing);
        }
        length -= kSquadGap;
        const bool aloft = at.speed > 0 && C::airborne(s);
        float front = length / 2;  // forward of the middle, where the squad in front starts
        int count = 0;
        for (size_t k = 0; k < s.armies.size(); ++k) {
            const sim::Army& a = s.armies[k];
            const Block& b = blocks[k];
            for (int i = 0; i < b.n; ++i) {
                const int row = i / b.cols, col = i % b.cols;
                const int inRow = row == b.rows - 1 ? b.n - row * b.cols : b.cols;
                const float u = (col - (inRow - 1) * 0.5f) * b.spacing;
                const float v = front - row * b.spacing * kRankDepth;
                MapSoldier m;
                m.id = s.id * 4096u + static_cast<uint32_t>(k) * 64u + static_cast<uint32_t>(i);
                const float jx = (hash01(m.id) - 0.5f) * 2 * kRagged, jy = (hash01(m.id + 7919u) - 0.5f) * 2 * kRagged;
                m.x = cx + fx * (v + jy) + rx * (u + jx);
                m.y = cy + fy * (v + jy) + ry * (u + jx);
                m.yaw = yaw;
                m.speed = at.speed;
                m.owner = s.owner;
                m.type = static_cast<uint8_t>(a.type);
                m.aloft = aloft;
                m.selected = s.id == selected;
                m.stack = s.id;
                soldiers.push_back(m);
                ++count;
            }
            front -= b.depth + kSquadGap;
        }
        StackShape sh;
        sh.id = s.id;
        sh.owner = s.owner;
        sh.x = cx;
        sh.y = cy;
        sh.yaw = yaw;
        sh.halfLength = std::max(2.0f, length * 0.5f + 1.0f);
        sh.halfWidth = std::max(2.0f, width * 0.5f + 1.0f);
        sh.radius = std::hypot(sh.halfLength, sh.halfWidth);
        sh.speed = at.speed;
        sh.soldiers = count;
        shapes.push_back(sh);
    }
    // Forget the stacks that are gone.
    facing.yaw = std::move(keep);
}

}  // namespace illyria::game
