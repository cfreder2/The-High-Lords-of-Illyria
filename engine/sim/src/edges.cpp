#include "illyria/sim/edges.h"

#include <algorithm>

#include "illyria/sim/region.h"

namespace illyria::sim {

int32_t Edges::total() const { return std::clamp(home + ground + seniority + hero + height + holding + realm, 0, kEdgeCapPct); }

std::string Edges::describe() const {
    std::string out;
    const auto add = [&](const char* what, int32_t pct) {
        if (pct <= 0) return;
        if (!out.empty()) out += " \xC2\xB7 ";  // a middle dot
        out += what;
        out += " +" + std::to_string(pct) + "%";
    };
    add("home land", home);
    add("the realm's own country", realm);
    add("familiar ground", ground);
    add("veterans", seniority);
    add("hero leads", hero);
    add("high ground", height);
    add("holding", holding);
    const int32_t sum = home + ground + seniority + hero + height + holding + realm;
    if (sum > kEdgeCapPct) out += " (at most +" + std::to_string(kEdgeCapPct) + "%)";
    return out;
}

uint8_t landOfGround(Ground ground, bool hilly) {
    switch (ground) {
        case Ground::Rock: return kLandHills;
        case Ground::Forest: return static_cast<uint8_t>(kLandForest | (hilly ? kLandHills : 0));
        case Ground::Swamp:
        case Ground::Mud: return kLandMarsh;
        case Ground::Shallows:
        case Ground::Sand:
        case Ground::Bridge: return kLandShore;
        case Ground::Plains:
        case Ground::Road: return hilly ? kLandHills : kLandOpen;
        default: return 0;
    }
}

uint8_t realmLand(uint8_t faction) {
    // The Aurelian Throne, the Highcrag Giants, the Ironhold Dwarves, the Orcs of Grath, the Sylvaran Elves, the
    // Sunreach Riders, the Moonward Order, Lord Malvoren.
    static constexpr uint8_t kRealm[8] = {kLandOpen, kLandHills, kLandHills, kLandMarsh | kLandHills, kLandForest,
                                          kLandOpen, kLandShore | kLandForest, kLandMarsh | kLandForest};
    return faction < 8 ? kRealm[faction] : 0;
}

const char* landName(uint8_t land) {
    switch (land) {
        case kLandOpen: return "the open country";
        case kLandForest: return "the woods";
        case kLandHills: return "the hills";
        case kLandMarsh: return "the fens";
        case kLandShore: return "the water's edge";
        case kLandMarsh | kLandHills: return "the fens and the hills";
        case kLandShore | kLandForest: return "the water's edge and the woods";
        case kLandMarsh | kLandForest: return "the fens and the dark woods";
        default: return "";
    }
}

int32_t heroLeadPct(int32_t s) { return s >= 9 ? 6 : s >= 7 ? 4 : s >= 4 ? 2 : 0; }

bool familiarGround(Gait gait, Ground ground) {
    if (gait == Gait::Foot || ground == Ground::Plains || ground == Ground::Road || ground == Ground::Bridge || ground == Ground::DeepWater)
        return false;
    return gaitSpeed(gait, ground) >= gaitSpeed(Gait::Foot, ground) + Fixed::fromRatio(15, 100);
}

}  // namespace illyria::sim
