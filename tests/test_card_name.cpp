// A squad's name on a card (game/include/illyria/game/card_name.h): whole if it fits, else two lines, else its small
// words dropped, else cut; never ending on "of" or "the", never wider than the card.
#include <string>
#include <vector>

#include "illyria/game/card_layout.h"
#include "illyria/game/card_name.h"
#include "test.h"

using namespace illyria::game;

namespace {

float chars(const std::string& s) { return static_cast<float>(s.size()); }  // one unit a letter

std::string fit(const std::string& name, float w, size_t lines, bool* whole = nullptr) {
    std::vector<std::string> out;
    const bool ok = fitCardName(name, chars, w, lines, out);
    if (whole) *whole = ok;
    std::string s;
    for (const auto& l : out) {
        CHECK(chars(l) <= w || w < 4);
        s += (s.empty() ? "" : "|") + l;
    }
    return s;
}

}  // namespace

TEST(card_name_fits_whole_when_it_can) {
    bool whole = false;
    CHECK(fit("Light Infantry", 20, 2, &whole) == "Light Infantry");
    CHECK(whole);
}

TEST(card_name_wraps_before_a_small_word) {
    bool whole = false;
    CHECK(fit("Brothers of the Root", 16, 2, &whole) == "Brothers|of the Root");
    CHECK(whole);
    CHECK(fit("Oakhaven Warriors", 12, 2, &whole) == "Oakhaven|Warriors");
}

TEST(card_name_drops_small_words_before_cutting) {
    bool whole = false;
    const std::string s = fit("Journeymen of the Moonhaven", 11, 2, &whole);
    CHECK(s == "Journeymen|Moonhaven");
    CHECK(whole);
}

TEST(card_name_cut_never_ends_on_of_or_a_gap) {
    const char* names[] = {"Journeymen of Moonhaven Heights", "Magi of the Tower of Ashes", "Guard of Moonhaven", "Wizards of Moo"};
    for (const char* n : names)
        for (float w = 6; w <= 14; w += 1) {
            bool whole = true;
            const std::string s = fit(n, w, 2, &whole);
            if (!whole) {
                CHECK(!s.empty() && s.back() == '.');
                CHECK(s.size() < 2 || s[s.size() - 2] != ' ');
                CHECK(s.size() < 4 || s.substr(s.size() - 4) != " of.");
                CHECK(s.size() < 5 || s.substr(s.size() - 5) != " the.");
            }
        }
}

TEST(card_name_is_deterministic) {
    CHECK(fit("Silverwood Archers of the Vale", 9, 2) == fit("Silverwood Archers of the Vale", 9, 2));
    CHECK(fit("", 9, 2) == "");
}

// The squad cards (card_layout.h): none is ever left out of reach, whatever the window and the number of squads.
TEST(card_layout_never_drops_a_squad) {
    for (const float w : {640.0f, 1000.0f, 1280.0f, 1600.0f, 1920.0f, 2600.0f}) {
        for (int n = 0; n <= 80; ++n) {
            const CardLayout l = layoutCards(n, w - 16);
            CHECK(l.columns >= 1 && l.rows >= 1 && l.rows <= kCardMaxRows);
            CHECK(l.width >= 96 && l.width <= 132);
            // a row of cards, gaps between, fits the room
            CHECK(l.columns * l.width + (l.columns - 1) * kCardGap <= w - 16 + 0.01f || l.columns == 1);
            // every card is on some page, and all the pages' cells fit the rows
            CHECK(l.perPage >= 1 || n == 0);
            CHECK(l.pages * l.perPage >= n);
            CHECK(l.perPage + (l.paged ? 1 : 0) <= l.rows * l.columns);
            if (!l.paged) CHECK(l.pages == 1 && l.perPage == n);
        }
    }
    // The Ford's 18 squads: one row at 1600 shrinks, two rows at 1280 and 1600 keep full cards, nothing is paged.
    CardLayout l = layoutCards(18, 1600 - 16);
    CHECK(!l.paged && l.rows <= 2);
    l = layoutCards(18, 1280 - 16);
    CHECK(!l.paged && l.rows == 2 && l.width == 132);
    l = layoutCards(11, 1600 - 16);  // what fitted before: still one full row
    CHECK(l.rows == 1 && l.width == 132);
    l = layoutCards(200, 1280 - 16);  // too many for three rows: paged
    CHECK(l.paged && l.pages > 1);
}
