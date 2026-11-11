// The side palette (game/include/illyria/game/palette.h): a colour the player chooses for their own side is cosmetic,
// and under every choice the sides stay apart from each other.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "illyria/game/battle.h"
#include "illyria/game/factions.h"
#include "illyria/game/minimap.h"
#include "illyria/game/mode.h"
#include "illyria/game/palette.h"
#include "test.h"

using namespace illyria::game;

namespace {

// Every faction's colour as the table now has it, pairwise at least kApart.
bool eightApart(float* least = nullptr) {
    float lo = 1e9f;
    for (int a = 0; a < palette::kFactions; ++a)
        for (int b = a + 1; b < palette::kFactions; ++b) lo = std::min(lo, palette::distance(palette::faction(static_cast<uint8_t>(a)), palette::faction(static_cast<uint8_t>(b))));
    if (least) *least = lo;
    return lo >= palette::kApart;
}

bool fourTeamsApart() {
    for (uint8_t a = 0; a < 4; ++a)
        for (uint8_t b = a + 1; b < 4; ++b)
            if (palette::distance(teamColour(a), teamColour(b)) < palette::kApart) return false;
    return true;
}

bool landApart(const V3& c) {
    V3 land[8];
    const int n = palette::land(land, 8);
    for (int i = 0; i < n; ++i)
        if (palette::distance(c, land[i]) < palette::kApartLand) return false;
    return true;
}

}  // namespace

TEST(palette_swatches_are_apart_from_each_other) {
    palette::clear();
    for (int a = 0; a < palette::kSwatches; ++a)
        for (int b = a + 1; b < palette::kSwatches; ++b) {
            const float d = palette::distance(palette::swatch(a).rgb, palette::swatch(b).rgb);
            if (d < palette::kApart) std::printf("  %s and %s are %.1f apart\n", palette::swatch(a).name, palette::swatch(b).name, static_cast<double>(d));
            CHECK(d >= palette::kApart);
        }
    // The four added to the lords' own keep clear of the ground and the water as well.
    for (int i = palette::kFactions; i < palette::kSwatches; ++i) CHECK(landApart(palette::swatch(i).rgb));
}

TEST(palette_default_is_the_lords_own_colours) {
    palette::clear();
    palette::bind(6);
    for (uint8_t f = 0; f < palette::kFactions; ++f) CHECK(palette::distance(palette::faction(f), palette::factionBase(f)) < 0.01f);
    CHECK(eightApart());
    CHECK(fourTeamsApart());
    // A skirmish's four seats are the Moonward blue, Ironhold red, Highcrag gold and Sylvaran green.
    for (uint8_t t = 0; t < 4; ++t) CHECK(palette::distance(teamColour(t), palette::factionBase(kSkirmishSeats[t])) < 0.01f);
}

TEST(palette_every_swatch_keeps_all_eight_sides_and_four_teams_apart) {
    for (uint8_t lord = 0; lord < palette::kFactions; ++lord) {
        for (int s = 0; s < palette::kSwatches; ++s) {
            palette::clear();
            palette::bind(lord);
            palette::choose(palette::swatch(s).rgb);
            float least = 0;
            if (!eightApart(&least)) std::printf("  lord %d choosing %s: %.1f\n", lord, palette::swatch(s).name, static_cast<double>(least));
            CHECK(eightApart());
            CHECK(fourTeamsApart());
            // The player wears what they chose; every other lord keeps his own, or took the player's old colour.
            CHECK(palette::distance(palette::faction(lord), palette::swatch(s).rgb) < 0.01f);
            int moved = 0;
            for (uint8_t f = 0; f < palette::kFactions; ++f) {
                if (f == lord) continue;
                if (palette::distance(palette::faction(f), palette::factionBase(f)) > 0.01f) {
                    ++moved;
                    CHECK(palette::distance(palette::faction(f), palette::factionBase(lord)) < 0.01f);  // the swap
                }
            }
            CHECK(moved <= 1);
            CHECK(moved == (s < palette::kFactions && s != lord ? 1 : 0));
        }
    }
    palette::clear();
    palette::bind(6);
}

TEST(palette_a_colour_of_their_own_is_checked_and_snaps) {
    int kept = 0, snapped = 0;
    for (uint8_t lord = 0; lord < palette::kFactions; ++lord) {
        for (int r = 0; r <= 10; ++r)
            for (int g = 0; g <= 10; ++g)
                for (int b = 0; b <= 10; ++b) {
                    const V3 want{static_cast<float>(r) / 10, static_cast<float>(g) / 10, static_cast<float>(b) / 10};
                    const V3 got = palette::snap(want, lord);
                    bool swatch = false;
                    for (int i = 0; i < palette::kSwatches; ++i) swatch |= palette::distance(got, palette::swatch(i).rgb) < 0.01f;
                    if (palette::distance(got, want) < 0.01f && !swatch) {
                        // Kept as it is: apart from every other lord and from the ground.
                        ++kept;
                        CHECK(landApart(got));
                        for (uint8_t f = 0; f < palette::kFactions; ++f)
                            if (f != lord) CHECK(palette::distance(got, palette::factionBase(f)) >= palette::kApart);
                    } else {
                        ++snapped;
                        CHECK(swatch);
                    }
                }
        // Chosen, none of it breaks the table.
        palette::clear();
        palette::bind(lord);
        for (int k = 0; k < 40; ++k) {
            palette::choose({static_cast<float>((k * 37) % 100) / 100, static_cast<float>((k * 53) % 100) / 100, static_cast<float>((k * 71) % 100) / 100});
            CHECK(eightApart());
            CHECK(fourTeamsApart());
        }
    }
    CHECK(kept > 0);
    CHECK(snapped > 0);
    palette::clear();
    palette::bind(6);
}

TEST(palette_one_place_for_every_use) {
    // The banner, the roofs, the dots and the sprites' key all ask factionColor: they follow the choice together.
    palette::clear();
    palette::bind(6);
    sideFactions()[0] = 6;
    palette::choose(palette::swatch(5).rgb);  // brown: the Sunreach Riders' colour, so they take the blue
    CHECK(palette::distance(factionColor(0), palette::swatch(5).rgb) < 0.01f);
    CHECK(palette::distance(bannerColor(0), factionColor(0)) < 0.01f);
    CHECK(palette::distance(teamColour(0), factionColor(0)) < 0.01f);
    CHECK(palette::distance(palette::faction(5), palette::factionBase(6)) < 0.01f);
    palette::clear();
    palette::bind(6);
    CHECK(palette::distance(factionColor(0), palette::factionBase(6)) < 0.01f);
}

TEST(palette_hex_round_trips) {
    V3 c;
    CHECK(palette::parseHex("#4a78e6", c));
    CHECK(palette::hex(c) == "#4a78e6");
    CHECK(!palette::parseHex("4a78e6", c));
    CHECK(!palette::parseHex("#4a78e", c));
    CHECK(!palette::parseHex("#4a78eg", c));
    CHECK(!palette::parseHex(nullptr, c));
}

// A battle's minimap is the same lines as the conquest's, in the side's colours, with every man the field shows.
TEST(battle_minimap_lists_the_men_in_their_sides_colours) {
    palette::clear();
    palette::bind(6);
    sideFactions()[0] = 6;
    sideFactions()[1] = 2;
    Battle ford(5, Scenario::Ford);
    const std::string text = battleMinimapText(ford);
    CHECK(text.rfind("m|1024|1024\n", 0) == 0);
    const auto count = [](const std::string& in, const std::string& colour) {
        size_t n = 0, at = 0;
        while ((at = in.find("|" + colour + "|", at)) != std::string::npos) ++n, at += 3;
        return n;
    };
    CHECK(count(text, palette::hex(factionColor(0))) > 0);
    CHECK(count(text, palette::hex(factionColor(1))) > 0);
    // Choosing another colour recolours the lines and nothing else.
    palette::choose(palette::swatch(8).rgb);
    const std::string purple = battleMinimapText(ford);
    CHECK(purple != text);
    CHECK(count(purple, palette::hex(palette::swatch(8).rgb)) > 0);
    palette::clear();
    palette::bind(6);
    CHECK(battleMinimapText(ford) == text);
    const std::vector<uint8_t> land = battleMinimapLand(ford, 64, 64);
    CHECK(land.size() == 64 * 64 * 4);
}

#ifdef ILLYRIA_WEB_DIR
// The page's menus draw the same palette before the engine has loaded (game/web/palette.js): its swatches are these.
TEST(palette_page_table_matches_the_engine) {
    std::ifstream in(std::string(ILLYRIA_WEB_DIR) + "/palette.js");
    std::stringstream text;
    text << in.rdbuf();
    const std::string js = text.str();
    CHECK(!js.empty());
    for (int i = 0; i < palette::kSwatches; ++i) {
        const std::string want = "\"" + palette::hex(palette::swatch(i).rgb) + "\"";
        const size_t at = js.find("// swatches");
        const size_t found = js.find(want, at);
        if (found == std::string::npos) std::printf("  palette.js lacks %s %s\n", palette::swatch(i).name, want.c_str());
        CHECK(found != std::string::npos);
    }
}
#endif
