// A squad's name on a card (game/include/illyria/game/card_name.h): whole if it fits, else two lines, else its small
// words dropped, else cut; never ending on "of" or "the", never wider than the card.
#include <string>
#include <vector>

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
