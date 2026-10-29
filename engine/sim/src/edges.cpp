#include "illyria/sim/edges.h"

#include <algorithm>

#include "illyria/sim/region.h"

namespace illyria::sim {

int32_t Edges::total() const { return std::clamp(home + ground + seniority + hero + height + holding, 0, kEdgeCapPct); }

std::string Edges::describe() const {
    std::string out;
    const auto add = [&](const char* what, int32_t pct) {
        if (pct <= 0) return;
        if (!out.empty()) out += " \xC2\xB7 ";  // a middle dot
        out += what;
        out += " +" + std::to_string(pct) + "%";
    };
    add("home land", home);
    add("familiar ground", ground);
    add("veterans", seniority);
    add("hero leads", hero);
    add("high ground", height);
    add("holding", holding);
    const int32_t sum = home + ground + seniority + hero + height + holding;
    if (sum > kEdgeCapPct) out += " (at most +" + std::to_string(kEdgeCapPct) + "%)";
    return out;
}

int32_t heroLeadPct(int32_t s) { return s >= 9 ? 6 : s >= 7 ? 4 : s >= 4 ? 2 : 0; }

bool familiarGround(Gait gait, Ground ground) {
    if (gait == Gait::Foot || ground == Ground::Plains || ground == Ground::Road || ground == Ground::Bridge || ground == Ground::DeepWater)
        return false;
    return gaitSpeed(gait, ground) >= gaitSpeed(Gait::Foot, ground) + Fixed::fromRatio(15, 100);
}

}  // namespace illyria::sim
