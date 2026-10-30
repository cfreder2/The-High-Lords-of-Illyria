// Saved games: a campaign written out and read back is the same campaign,
// and carries on exactly as the original would have (campaign_save.cpp).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

void days(Campaign& c, int n) {
    for (int t = 0; t < Campaign::kTicksPerDay * n; ++t) c.step();
}

int32_t capitalOf(const Campaign& c, uint8_t side) {
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.cities()[i].owner == side) return static_cast<int32_t>(i);
    return -1;
}
}  // namespace

TEST(a_saved_campaign_restores_and_carries_on_identically) {
    // Every lord on the computer for a while: boats, heroes, quests, banners,
    // transits and the land all have something in them by then.
    Campaign c(continent(), 77);
    days(c, 30);
    // The player takes over side 0 and gives a few orders before saving.
    c.setHuman(0, true);
    const int32_t capital = capitalOf(c, 0);
    if (capital >= 0) c.produce(capital, 0);
    days(c, 2);
    const std::string text = c.save();
    CHECK(text.size() > 100);
    std::printf("  day %u saved in %zu characters\n", c.day(), text.size());
    std::string why;
    std::unique_ptr<Campaign> r = Campaign::load(continent(), text, &why);
    CHECK(r != nullptr);
    if (!r) {
        std::printf("  load refused: %s\n", why.c_str());
        return;
    }
    CHECK(r->checksum() == c.checksum());
    CHECK(r->save() == text);
    CHECK(r->tick() == c.tick());
    CHECK(r->seed() == 77);
    // Both go on for ten more days under the same orders: identical every day.
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == 0 && !s.armies.empty()) {
            const int32_t tx = std::min(s.cx + 6, c.width() - 1);
            CHECK(c.move(s.id, tx, s.cy) == r->move(s.id, tx, s.cy));
            break;
        }
    bool same = true;
    for (int d = 0; d < 10; ++d) {
        days(c, 1);
        days(*r, 1);
        if (c.pending()) {
            c.resolvePending();
            r->resolvePending();
        }
        same = same && c.checksum() == r->checksum();
    }
    CHECK(same);
    CHECK(r->save() == c.save());
}

TEST(a_campaign_saved_with_a_battle_waiting_restores_the_battle) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    // March the player's stack on the nearest neutral town until the battle waits.
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    CHECK(home != nullptr);
    int best = 1 << 30, target = -1;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& cs = c.cities()[i];
        if (cs.owner != Campaign::kNeutral) continue;
        const int d = std::abs(cs.cx - home->cx) + std::abs(cs.cy - home->cy);
        if (d < best) {
            best = d;
            target = static_cast<int>(i);
        }
    }
    CHECK(target >= 0);
    const uint32_t id = home->id;
    c.move(id, c.cities()[static_cast<size_t>(target)].cx, c.cities()[static_cast<size_t>(target)].cy);
    for (int t = 0; t < Campaign::kTicksPerDay * 20 && !c.pending(); ++t) c.step();
    CHECK(c.pending() != nullptr);
    std::unique_ptr<Campaign> r = Campaign::load(continent(), c.save());
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->pending() != nullptr);
    CHECK(r->pendingAttackers().size() == c.pendingAttackers().size());
    c.resolvePending();
    r->resolvePending();
    days(c, 3);
    days(*r, 3);
    CHECK(r->checksum() == c.checksum());
}

TEST(a_damaged_or_foreign_save_is_refused) {
    Campaign c(continent(), 5);
    days(c, 3);
    const std::string text = c.save();
    std::string why;
    CHECK(Campaign::load(continent(), "", &why) == nullptr);
    CHECK(!why.empty());
    CHECK(Campaign::load(continent(), "hello", &why) == nullptr);
    CHECK(Campaign::load(continent(), text.substr(0, text.size() / 2), &why) == nullptr);
    // One gold piece more for a side somewhere in the middle: the checksum catches it.
    std::string bent = text;
    const size_t k = bent.size() / 3;
    bent[k] = bent[k] == 'A' ? 'B' : 'A';
    CHECK(Campaign::load(continent(), bent, &why) == nullptr);
    CHECK(Campaign::load(continent(), text, &why) != nullptr);
}
