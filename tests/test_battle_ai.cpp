// The computer's battle AI (Battle::aiFight, pickFoe, runEnemyAi), measured on
// small deterministic fields: what the Normal computer does that the Easy one
// does not. Squads are raised on an empty field (the player's side idle).
#include <cmath>
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/game/mode.h"
#include "test.h"

using namespace illyria;
using namespace illyria::game;
using sim::UnitType;

namespace {

std::shared_ptr<const sim::Continent> continent() {
    static const auto c = std::make_shared<const sim::Continent>(Battle::kRegionSeed);
    return c;
}

std::unique_ptr<Battle> field(Battle::AiSkill skill) {
    auto b = std::make_unique<Battle>(5, battlePlaces(*continent()).front().pos, continent());
    b->setAiSkill(skill);
    return b;
}

void run(Battle& b, int ticks) {
    for (int i = 0; i < ticks; ++i) b.advance(1.0 / sim::kTicksPerSecond + 1e-6);
}

V3 centreOf(const Battle& b, int q) { return b.squadCentre(b.squads()[static_cast<size_t>(q)], false); }

const V3 kWest{-1, 0, 0}, kEast{1, 0, 0};
constexpr sim::PlayerId kMe = Battle::kPlayer, kThem = Battle::kEnemy;

void kill(Battle& b, int q, int leave) {
    int n = 0;
    for (sim::EntityId id : b.squads()[static_cast<size_t>(q)].members)
        if (++n > leave) const_cast<sim::Unit&>(*b.world().findUnit(id)).hp = 0;
}

// How far (metres) the enemy archers stood ahead of the enemy foot toward the
// player's side, at its worst, over a march and the first clash.
float worstArcherLead(Battle::AiSkill skill) {
    auto b = field(skill);
    const V3 o = V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
    const int foe = b->addSquad("Foe", kMe, UnitType::Warrior, 12, o + V3{-130, 0, 0}, kEast);
    (void)foe;
    const int foot = b->addSquad("Foot", kThem, UnitType::HeavyInfantry, 12, o + V3{40, 0, 0}, kWest);
    const int bows = b->addSquad("Bows", kThem, UnitType::Archers, 12, o + V3{44, 14, 0}, kWest);
    float worst = -1e9f;
    for (int t = 0; t < 40; ++t) {
        run(*b, 60);
        // Toward the west is "ahead": positive when the bows are west of the foot.
        worst = std::max(worst, centreOf(*b, foot).x - centreOf(*b, bows).x);
    }
    return worst;
}

}  // namespace

TEST(ai_archers_stay_behind_the_foot) {
    const float easy = worstArcherLead(Battle::AiSkill::Easy);
    const float normal = worstArcherLead(Battle::AiSkill::Normal);
    std::printf("  archers ahead of the foot at worst: easy %.1f m, normal %.1f m\n", easy, normal);
    CHECK(easy > 4.0f);     // the easy computer lets them stand in front
    CHECK(normal < 3.0f);   // the normal one never does
}

TEST(ai_shattered_squad_falls_back_on_its_friends) {
    for (const auto skill : {Battle::AiSkill::Easy, Battle::AiSkill::Normal}) {
        auto b = field(skill);
        const V3 o = V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
        b->addSquad("Hunters", kMe, UnitType::Warrior, 12, o + V3{-24, 0, 0}, kEast);
        const int hurt = b->addSquad("Hurt", kThem, UnitType::Warrior, 12, o + V3{0, 0, 0}, kWest);
        const int friendly = b->addSquad("Rest", kThem, UnitType::Warrior, 12, o + V3{60, 0, 0}, kWest);
        kill(*b, hurt, 3);
        const bool shattered = b->aiShattered(b->squads()[static_cast<size_t>(hurt)]);
        CHECK(shattered);
        CHECK(!b->aiShattered(b->squads()[static_cast<size_t>(friendly)]));
        const float before = centreOf(*b, hurt).x;
        run(*b, 240);
        const float after = centreOf(*b, hurt).x;
        std::printf("  %s: the shattered squad moved %+.1f m (east is home)\n", skill == Battle::AiSkill::Easy ? "easy" : "normal",
                    after - before);
        if (skill == Battle::AiSkill::Normal) CHECK(after - before > 4.0f);
        else CHECK(after - before < 8.0f);
    }
    // Cornered, with nobody to fall back on, a shattered squad fights on.
    auto b = field(Battle::AiSkill::Normal);
    const int lone = b->addSquad("Lone", kThem, UnitType::Warrior, 12, V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0}, kWest);
    kill(*b, lone, 3);
    CHECK(!b->aiShattered(b->squads()[static_cast<size_t>(lone)]));
}

TEST(ai_masses_before_it_engages) {
    // A fast squad of horse and a slow body of foot, the foe far off: the
    // easy computer (once it marches: it stands the first 45 s, Battle::kEasyWaitTicks) lets the horse arrive alone; the
    // normal one keeps the army together.
    const auto spread = [](Battle::AiSkill skill) {
        auto b = field(skill);
        const V3 o = V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
        b->addSquad("Foe", kMe, UnitType::Warrior, 12, o + V3{-200, 0, 0}, kEast);
        const int horse = b->addSquad("Horse", kThem, UnitType::LightCavalry, 8, o + V3{60, 0, 0}, kWest);
        const int foot = b->addSquad("Foot", kThem, UnitType::Pikemen, 12, o + V3{110, 0, 0}, kWest);
        float worst = 0;
        for (int t = 0; t < 36; ++t) {
            run(*b, 60);
            if (t == 20 && skill == Battle::AiSkill::Easy) CHECK(std::abs(centreOf(*b, foot).x - centreOf(*b, horse).x - 50.0f) < 2.0f);  // (40 s in: Easy has not stirred)
            worst = std::max(worst, centreOf(*b, foot).x - centreOf(*b, horse).x);
        }
        return worst;
    };
    const float easy = spread(Battle::AiSkill::Easy), normal = spread(Battle::AiSkill::Normal);
    std::printf("  horse ahead of the foot at worst: easy %.1f m, normal %.1f m\n", easy, normal);
    CHECK(normal < easy - 15.0f);
}

TEST(ai_goes_for_the_healer_and_the_archers_before_the_nearest_shield) {
    const V3 o = V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
    for (const auto skill : {Battle::AiSkill::Easy, Battle::AiSkill::Normal}) {
        auto b = field(skill);
        const int shield = b->addSquad("Shield", kMe, UnitType::Warrior, 6, o + V3{-20, 0, 0}, kEast);
        const int bows = b->addSquad("Bows", kMe, UnitType::Archers, 6, o + V3{-26, 0, 0}, kEast);
        const int pick = b->addSquad("Pick", kThem, UnitType::Warrior, 6, o, kWest);
        (void)shield;
        (void)pick;
        const sim::EntityId foe = b->pickFoe(kThem, o, 100.0f, true);
        CHECK(foe != sim::kNoEntity);
        const int owner = foe == sim::kNoEntity ? -1 : b->squadOf(foe);
        std::printf("  %s picks %s\n", skill == Battle::AiSkill::Easy ? "easy" : "normal", owner == bows ? "the bows" : "the shield");
        CHECK(owner == (skill == Battle::AiSkill::Easy ? shield : bows));
    }
}

TEST(ai_is_deterministic) {
    const auto world = [] {
        auto b = field(Battle::AiSkill::Normal);
        const V3 o = V3{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
        b->addSquad("A", kMe, UnitType::Warrior, 12, o + V3{-100, 0, 0}, kEast);
        b->addSquad("B", kThem, UnitType::Warrior, 12, o + V3{20, 0, 0}, kWest);
        b->addSquad("C", kThem, UnitType::Archers, 12, o + V3{45, 0, 0}, kWest);
        b->addSquad("D", kThem, UnitType::LightCavalry, 8, o + V3{60, 10, 0}, kWest);
        run(*b, 900);
        return b->world().checksum();
    };
    CHECK(world() == world());
}
