// The squad cards (game/include/illyria/game/card_layout.h, card_face.h): one row when the squads fit, two compact rows
// in a wide window, pages only past that; the order by role with the rare ones at the right-hand end; every card and
// the "+N more" cell under the press that is on it; the card's slots clear of its name.
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "illyria/game/card_face.h"
#include "illyria/game/card_layout.h"
#include "test.h"

using namespace illyria::game;

// None is ever left out of reach, whatever the window and the number of squads.
TEST(card_layout_never_drops_a_squad) {
    for (const float w : {390.0f, 640.0f, 1000.0f, 1280.0f, 1600.0f, 1920.0f, 2600.0f}) {
        for (int n = 0; n <= 80; ++n) {
            const CardLayout l = layoutCards(n, w - 16, cardRowsFor(w));
            CHECK(l.columns >= 1 && l.rows >= 1 && l.rows <= cardRowsFor(w));
            CHECK(l.width >= 96 && l.width <= 132);
            CHECK(l.columns * l.width + (l.columns - 1) * kCardGap <= w - 16 + 0.01f || l.columns == 1);  // a row fits the room
            CHECK(l.perPage >= 1 || n == 0);
            CHECK(l.pages * l.perPage >= n);
            CHECK(l.perPage + (l.paged ? 1 : 0) <= l.rows * l.columns);
            if (!l.paged) CHECK(l.pages == 1 && l.perPage == n);
            CHECK(l.compact == (l.rows == 2));
            CHECK(l.height == (l.compact ? kCardCompactH : kCardH));
        }
    }
}

TEST(card_layout_one_row_while_it_fits_then_two_compact_rows_then_pages) {
    // 1600 wide: 8 and 11 squads are one row of full cards, as they always were; 14 still one row, narrower.
    CardLayout l = layoutCards(8, 1600 - 16, cardRowsFor(1600));
    CHECK(l.rows == 1 && !l.compact && !l.paged && l.width == 132 && l.height == 62);
    l = layoutCards(11, 1600 - 16, cardRowsFor(1600));
    CHECK(l.rows == 1 && !l.paged && l.width == 132);
    l = layoutCards(14, 1600 - 16, cardRowsFor(1600));
    CHECK(l.rows == 1 && !l.paged && l.width == 96);
    // The March's 20: two rows of compact cards at their full width, nothing paged; two rows stand 82 points.
    l = layoutCards(20, 1600 - 16, cardRowsFor(1600));
    CHECK(l.rows == 2 && l.compact && !l.paged && l.width == 132 && l.columns == 11);
    CHECK(cardStripHeight(l) == 2 * kCardCompactH + kCardGap);
    CHECK(kCardCompactH <= 0.65f * kCardH && kCardCompactH >= 0.55f * kCardH);  // about 60% of the full card
    // 30: still two rows at 1600 (15 a row), no paging.
    l = layoutCards(30, 1600 - 16, cardRowsFor(1600));
    CHECK(l.rows == 2 && !l.paged && l.columns * 2 >= 30);
    // 1280 wide: 14 and 20 in two rows, 30 pages (two rows of 12, the last cell the arrows).
    l = layoutCards(14, 1280 - 16, cardRowsFor(1280));
    CHECK(l.rows == 2 && !l.paged);
    l = layoutCards(20, 1280 - 16, cardRowsFor(1280));
    CHECK(l.rows == 2 && !l.paged);
    l = layoutCards(30, 1280 - 16, cardRowsFor(1280));
    CHECK(l.rows == 2 && l.paged && l.compact && l.perPage == 2 * l.columns - 1 && l.pages == 2);
    // A phone: one row of full cards and pages, never a second row.
    CHECK(cardRowsFor(390) == 1 && cardRowsFor(899) == 1 && cardRowsFor(900) == 2);
    l = layoutCards(8, 390 - 16, cardRowsFor(390));
    CHECK(l.rows == 1 && l.paged && !l.compact && l.height == kCardH && l.perPage == l.columns - 1 && l.pages * l.perPage >= 8);
    l = layoutCards(3, 390 - 16, cardRowsFor(390));
    CHECK(l.rows == 1 && !l.paged);
    l = layoutCards(200, 1280 - 16, cardRowsFor(1280));
    CHECK(l.paged && l.pages > 1 && l.rows == 2);
}

TEST(card_order_is_foot_bows_horse_then_the_rare_ones_and_stable_within_a_role) {
    const std::vector<std::string> types = {"Hero",          "Archers",       "Heavy Infantry", "Catapults", "Light Cavalry", "Pikemen", "Wizards",
                                            "Heavy Cavalry", "Light Infantry", "Archers",        "Dwarven Grenadiers", "Warriors", "Catapults", "Hyena-folk"};
    std::vector<int> ranks;
    for (const std::string& t : types) ranks.push_back(cardRoleRank(t));
    const std::vector<int> order = cardOrder(ranks);
    std::vector<std::string> got;
    for (int i : order) got.push_back(types[static_cast<size_t>(i)]);
    const std::vector<std::string> want = {"Heavy Infantry", "Pikemen",       "Light Infantry", "Warriors",           "Hyena-folk", "Archers",   "Archers",
                                           "Light Cavalry",  "Heavy Cavalry", "Dwarven Grenadiers", "Wizards", "Catapults",  "Catapults", "Hero"};
    CHECK(got == want);
    // Stable: the two Archers and the two Catapults keep the muster's order.
    const std::vector<int> wantIndex = {2, 5, 8, 11, 13, 1, 9, 4, 7, 10, 6, 3, 12, 0};
    CHECK(order == wantIndex);
    // Foot, bows and horse are not rare; the grenadiers on are.
    CHECK(cardRoleRank("Pikemen") == 0 && cardRoleRank("Archers") == 1 && cardRoleRank("Heavy Cavalry") == 2);
    CHECK(cardRoleRank("Dwarven Grenadiers") == kCardRareRank && cardRoleRank("Wizards") > kCardRareRank);
    CHECK(cardRoleRank("Catapults") > cardRoleRank("Wizards") && cardRoleRank("Hero") > cardRoleRank("Catapults"));
    CHECK(cardRoleRank("something new") == 0);
    CHECK(cardOrder({}).empty());
}

TEST(card_cells_fill_from_the_left_and_the_rare_ones_from_the_right_hand_end) {
    // One row, 8 squads of which 3 are rare, 11 columns: five from the left, a gap, three at the right-hand end.
    CardLayout l = layoutCards(8, 1600 - 16, 2);
    CHECK(l.columns == 11);
    for (int k = 0; k < 5; ++k) CHECK(cardCell(l, 8, 3, 0, k) == k);
    CHECK(cardCell(l, 8, 3, 0, 5) == 8 && cardCell(l, 8, 3, 0, 6) == 9 && cardCell(l, 8, 3, 0, 7) == 10);
    // Two rows, 20 squads, 4 rare: the lord is the last cell of the second row whatever the count.
    l = layoutCards(20, 1600 - 16, 2);
    CHECK(cardCell(l, 20, 4, 0, 19) == 2 * l.columns - 1);
    CHECK(cardCell(l, 20, 4, 0, 16) == 2 * l.columns - 4);
    CHECK(cardCell(l, 20, 4, 0, 15) == 15 && cardCell(l, 20, 4, 0, 0) == 0);
    // No two cards share a cell, in any strip; every card is on exactly one page; the pager's cell is no card's.
    for (const float w : {390.0f, 1280.0f, 1600.0f})
        for (int n = 1; n <= 70; ++n)
            for (int rare : {0, 1, 4, n}) {
                const CardLayout s = layoutCards(n, w - 16, cardRowsFor(w));
                int seen = 0;
                for (int page = 0; page < s.pages; ++page) {
                    std::set<int> cells;
                    for (int k = 0; k < n; ++k) {
                        const int cell = cardCell(s, n, rare, page, k);
                        if (cell < 0) continue;
                        ++seen;
                        CHECK(cell < s.rows * s.columns - (s.paged ? 1 : 0));
                        CHECK(cells.insert(cell).second);
                    }
                }
                CHECK(seen == n);
                // The last card (the lord) is the last cell there is for a card, on the last page.
                if (rare > 0) CHECK(cardCell(s, n, rare, s.pages - 1, n - 1) == s.rows * s.columns - 1 - (s.paged ? 1 : 0));
            }
    CHECK(cardCell(l, 20, 4, 0, 20) == -1 && cardCell(l, 20, 4, 0, -1) == -1);
}

TEST(card_hits_both_rows_and_the_more_cell) {
    const float x0 = 8, y0 = 800;
    // Two rows, not paged: a press in the middle of each card's cell is that card; the gap between the rows is none.
    CardLayout l = layoutCards(20, 1600 - 16, 2);
    for (int k = 0; k < 20; ++k) {
        const CardRect r = cardCellRect(l, cardCell(l, 20, 4, 0, k), x0, y0);
        const CardHit hit = cardAt(l, 20, 4, 0, x0, y0, r.x + r.w / 2, r.y + r.h / 2);
        CHECK(hit.what == CardHit::Card && hit.card == k);
    }
    CardRect first = cardCellRect(l, 0, x0, y0), second = cardCellRect(l, l.columns, x0, y0);
    CHECK(second.y == first.y + kCardCompactH + kCardGap && second.x == first.x);
    CHECK(cardAt(l, 20, 4, 0, x0, y0, first.x + 5, first.y + kCardCompactH + 2).what == CardHit::None);  // between the rows
    CHECK(cardAt(l, 20, 4, 0, x0, y0, first.x + 5, first.y + 5).card == 0);
    CHECK(cardAt(l, 20, 4, 0, x0, y0, second.x + 5, second.y + 5).card == l.columns);
    CHECK(cardAt(l, 20, 4, 0, x0, y0, first.x + 5, y0 - 1).what == CardHit::None);
    CHECK(cardAt(l, 20, 4, 0, x0, y0, cardCellRect(l, 16, x0, y0).x + 5, second.y + 5).what == CardHit::None);  // an empty cell (between the horse and the rare ones)
    // Paged (30 at 1280): the last cell is "+N more"; its ends are the arrows, its middle the next page, round.
    l = layoutCards(30, 1280 - 16, 2);
    CHECK(l.paged && l.pages == 2);
    const CardRect pager = cardPagerRect(l, x0, y0);
    CHECK(pager.y == y0 + kCardCompactH + kCardGap);  // (the second row)
    CHECK(cardAt(l, 30, 4, 0, x0, y0, pager.x + 4, pager.y + 10).what == CardHit::Back);
    CHECK(cardAt(l, 30, 4, 0, x0, y0, pager.x + pager.w - 4, pager.y + 10).what == CardHit::On);
    CHECK(cardAt(l, 30, 4, 0, x0, y0, pager.x + pager.w / 2, pager.y + 10).what == CardHit::More);
    CHECK(cardPageAfter(l, 0, CardHit::More) == 1 && cardPageAfter(l, 1, CardHit::More) == 0);
    CHECK(cardPageAfter(l, 0, CardHit::Back) == 0 && cardPageAfter(l, 1, CardHit::Back) == 0);
    CHECK(cardPageAfter(l, 0, CardHit::On) == 1 && cardPageAfter(l, 1, CardHit::On) == 1);
    CHECK(cardsElsewhere(l, 30, 0) == 30 - l.perPage && cardsElsewhere(l, 30, 1) == l.perPage);
    // Each page's cards are under their own cells, in both rows; the other page's are not there.
    for (int page = 0; page < l.pages; ++page)
        for (int k = 0; k < 30; ++k) {
            const int cell = cardCell(l, 30, 4, page, k);
            CHECK((cell >= 0) == (k / l.perPage == page));
            if (cell < 0) continue;
            const CardRect r = cardCellRect(l, cell, x0, y0);
            const CardHit hit = cardAt(l, 30, 4, page, x0, y0, r.x + 2, r.y + r.h - 2);
            CHECK(hit.what == CardHit::Card && hit.card == k);
        }
    // A phone's one row: the pager is its last cell.
    l = layoutCards(8, 390 - 16, 1);
    const CardRect phone = cardPagerRect(l, x0, y0);
    CHECK(phone.y == y0 && phone.x + phone.w <= 390 - 8 + 0.01f);
    CHECK(cardAt(l, 8, 1, 0, x0, y0, phone.x + phone.w / 2, y0 + 30).what == CardHit::More);
    CHECK(cardAt(l, 8, 1, 0, x0, y0, x0 + 5, y0 + 30).card == 0);
}

TEST(card_face_slots_never_overlap_the_name) {
    for (const bool compact : {false, true})
        for (const float cw : kCardWidths)
            for (const float counterW : {0.0f, 12.0f, 34.0f})
                for (const float pipsW : {0.0f, 4.0f, 16.0f}) {
                    const float ch = compact ? kCardCompactH : kCardH;
                    const CardFace f = cardFace(cw, ch, compact, counterW, pipsW);
                    CHECK(f.nameLines == (compact ? 1 : 2));
                    CHECK(f.nameX + f.nameW <= cw - f.pad + 0.01f);
                    if (counterW > 0) CHECK(f.nameX + f.nameW < f.counterX);  // the counter slot is right of the name's room
                    CHECK(f.counterX + counterW <= cw - f.pad + 0.01f);
                    CHECK(f.nameW >= 46);  // room for a word still
                    // The name's lines end above the count's line, and that above the bar.
                    if (compact) CHECK(f.nameY + f.lineH <= f.rowY);
                    CHECK(f.rowY + (compact ? 0 : f.lineH) + 11 <= f.barY + 0.01f);
                    CHECK(f.barY + f.barH <= ch - 3);
                    if (pipsW > 0) CHECK(f.barX + f.barW < f.pipsX);  // the pips are past the bar's end
                    CHECK(f.pipsX + pipsW <= cw - f.pad + 0.01f);
                }
}

TEST(card_status_is_one_mark_the_most_urgent) {
    CardFacts f;
    CHECK(cardStatus(f) == CardStatus::None && cardStatusWord(f).empty());
    f.idle = true;
    CHECK(cardStatusWord(f) == "IDLE");
    f.hold = true;
    CHECK(cardStatusWord(f) == "HOLD");
    f.answering = true;
    CHECK(cardStatusWord(f) == "ANSWER");
    f.calling = true;
    CHECK(cardStatusWord(f) == "HELP");
    f.rallyIn = 12;
    CHECK(cardStatus(f) == CardStatus::Broken && cardStatusWord(f) == "12s");
    f.rallyIn = -1;
    CHECK(cardStatusWord(f) == "--");
    // The counter slot: satchels whenever it carries any, a small store always, a big one (arrows) only when low.
    CardFacts g;
    CHECK(!cardShowsSatchels(g) && !cardShowsAmmo(g));
    g.satMax = 3, g.ammoMax = 4, g.ammo = 4;
    CHECK(cardShowsSatchels(g) && cardShowsAmmo(g));
    g.ammoMax = 60, g.ammo = 40;
    CHECK(!cardShowsAmmo(g));
    g.ammo = 15;
    CHECK(cardShowsAmmo(g));
    g.rank = 7;
    CHECK(cardRankPips(g) == 3);
    CHECK(cardGroundTag("in the ford") == "ford" && cardGroundTag("high ground") == "high" && cardGroundTag("in woods") == "woods" && cardGroundTag("").empty());
}

#ifdef ILLYRIA_WEB_DIR
// The page's strip (game/web/index.html, cardStrip) follows the same rule with the same numbers: a constant changed
// here and not there fails.
TEST(card_layout_page_constants_match) {
    std::ifstream in(std::string(ILLYRIA_WEB_DIR) + "/index.html");
    std::stringstream text;
    text << in.rdbuf();
    const std::string page = text.str();
    CHECK(!page.empty());
    char want[96];
    std::snprintf(want, sizeof want, "const kCardTwoRowWidth = %d, kCardMaxRows = %d;", static_cast<int>(kCardTwoRowWidth), kCardMaxRows);
    if (page.find(want) == std::string::npos) std::printf("  index.html lacks: %s\n", want);
    CHECK(page.find(want) != std::string::npos);
    std::snprintf(want, sizeof want, "const kCardRoleOrder = ");
    CHECK(page.find(want) != std::string::npos);
    // The role table there names the same types in the same ranks.
    for (const char* type : {"Archers", "Light Cavalry", "Heavy Cavalry", "Wolf Riders", "Dwarven Grenadiers", "Wizards", "Catapults", "Hero"}) {
        std::snprintf(want, sizeof want, "\"%s\": %d", type, cardRoleRank(type));
        if (page.find(want) == std::string::npos) std::printf("  index.html lacks: %s\n", want);
        CHECK(page.find(want) != std::string::npos);
    }
}
#endif
