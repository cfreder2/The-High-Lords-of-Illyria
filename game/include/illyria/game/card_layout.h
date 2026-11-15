#pragma once

// Where the squad cards of the HUD go (game/native/hud.cpp; the page's strip, game/web/index.html, mirrors the rule:
// see "The page" below). Every squad gets a card, always.
//
//  - One row of full cards (62 points tall) when the squads fit in one, the card narrowing a step or two first
//    (132, 112, 96 points: widths the name still reads in). A small army loses nothing.
//  - A wide window (kCardTwoRowWidth points or more) takes a SECOND row when one will not do, of compact cards
//    (38 points tall: name, count, one status mark, the bar), so two rows stand 82 points against one row's 62.
//  - Only when two rows overflow does the strip page: the last cell is "+N more" with its arrows.
//  - A narrow window (a phone) keeps one row of full cards and pages.
//
// The order is by role, not by muster: foot, bows, horse, then the rare ones (grenadiers, wizards and the other
// oddities, catapults, the lord or hero last), the order within a role as mustered. The rare ones are set from the
// right-hand end of the strip's last row backwards, so the lord is always the bottom right card and the catapults
// beside him, however many foot there are (cardCell).
//
// The page: game/web/index.html has `cardStrip` with the same constants and the same row rule; tests/test_card_layout.cpp
// reads the page and fails when a constant there differs from the one here.

#include <algorithm>
#include <string>
#include <vector>

namespace illyria::game {

struct CardLayout {
    float width = 132;    // one card
    float height = 62;    // and its height: kCardH, or kCardCompactH in a strip of two rows
    int rows = 1;         // rows of cards in the strip
    int columns = 0;      // cards in a row
    int perPage = 0;      // cards on a page (all of them unless paged)
    int pages = 1;
    bool paged = false;   // the last cell of the last row holds the page arrows
    bool compact = false; // the compact card (two rows)
};

constexpr float kCardGap = 6;
constexpr float kCardWidths[] = {132, 112, 96};  // the card's steps, widest first
constexpr float kCardH = 62, kCardCompactH = 38;
constexpr int kCardMaxRows = 2;
constexpr float kCardTwoRowWidth = 900;  // a window this wide (points) or wider may take a second row
constexpr float kCardPagerArrow = 24;    // the "+N more" cell: this much at each end is an arrow, the middle turns the page on

// The rows a window of that width may use: two on a desktop, one on a phone or a narrow window.
inline int cardRowsFor(float windowWidth) { return windowWidth >= kCardTwoRowWidth ? 2 : 1; }

// `count` cards in `avail` points of width (the screen less its margins), in at most `maxRows` rows
// (cardRowsFor of the window).
inline CardLayout layoutCards(int count, float avail, int maxRows = kCardMaxRows) {
    CardLayout out;
    count = std::max(count, 0);
    const auto columnsAt = [&](float width) { return std::max(1, static_cast<int>((avail + kCardGap) / (width + kCardGap))); };
    const auto shape = [&](int rows) {
        out.rows = rows;
        out.compact = rows > 1;
        out.height = out.compact ? kCardCompactH : kCardH;
    };
    // Rows first, widest card first: one row of full cards, one of narrower, then two rows of full width, and so on.
    maxRows = std::clamp(maxRows, 1, kCardMaxRows);
    for (int rows = 1; rows <= maxRows; ++rows)
        for (const float width : kCardWidths) {
            const int columns = columnsAt(width);
            if (columns * rows >= count) {
                out.width = width;
                shape(std::max(1, (count + columns - 1) / columns));  // (rows in use: no empty row's worth of space)
                out.columns = columns;
                out.perPage = count;
                out.pages = 1;
                return out;
            }
        }
    // Too many even so: the narrowest card, the most rows, a page at a time; the last cell is the arrows.
    out.width = kCardWidths[sizeof kCardWidths / sizeof *kCardWidths - 1];
    shape(maxRows);
    out.columns = columnsAt(out.width);
    out.paged = true;
    out.perPage = std::max(1, out.rows * out.columns - 1);
    out.pages = (count + out.perPage - 1) / out.perPage;
    return out;
}

// --- The order ----------------------------------------------------------------------------------------------------

// A squad's place in the strip by what it is (its type's name, sim::unitTypeName; the state JSON's "type"): 0 foot,
// 1 bows, 2 horse, and from kCardRareRank the rare ones in the order they stand at the right-hand end. A type not
// named here is foot.
constexpr int kCardRareRank = 3;
inline int cardRoleRank(const std::string& type) {
    static const char* const kBows[] = {"Archers"};
    static const char* const kHorse[] = {"Light Cavalry", "Heavy Cavalry", "Wolf Riders", "Elephants", "Unicorns", "Griffins", "Pegasi"};
    static const char* const kDwarf[] = {"Dwarven Grenadiers", "Dwarven Legion"};
    static const char* const kMagic[] = {"Wizards", "Journeymen", "Dragons", "Devils", "Demons", "Archons"};
    const auto in = [&](const char* const* list, size_t n) { return std::any_of(list, list + n, [&](const char* s) { return type == s; }); };
    if (type == "Hero") return kCardRareRank + 3;
    if (type == "Catapults") return kCardRareRank + 2;
    if (in(kMagic, sizeof kMagic / sizeof *kMagic)) return kCardRareRank + 1;
    if (in(kDwarf, sizeof kDwarf / sizeof *kDwarf)) return kCardRareRank;
    if (in(kHorse, sizeof kHorse / sizeof *kHorse)) return 2;
    if (in(kBows, sizeof kBows / sizeof *kBows)) return 1;
    return 0;
}

// The cards' order: positions into `ranks` (one a squad, as mustered), by rank, the muster's order kept within one.
inline std::vector<int> cardOrder(const std::vector<int>& ranks) {
    std::vector<int> order(ranks.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return ranks[static_cast<size_t>(a)] < ranks[static_cast<size_t>(b)]; });
    return order;
}

// --- Cells and hits -----------------------------------------------------------------------------------------------

// The cell (row * columns + column) of the k-th card in order, on `page`; -1 when it is on another page. The last
// `rare` cards of the order are set from the end of the strip backwards: on one page the lord is the last cell of the
// last row, on a paged strip (whose last cell is the arrows) the cell before it, on the last page.
inline int cardCell(const CardLayout& l, int count, int rare, int page, int k) {
    if (k < 0 || k >= count || l.columns <= 0) return -1;
    rare = std::clamp(rare, 0, count);
    if (!l.paged) {
        const int cells = l.rows * l.columns;
        return k >= count - rare ? cells - (count - k) : k;
    }
    if (k / l.perPage != page) return -1;
    const int onLast = count - (l.pages - 1) * l.perPage;  // cards on the last page
    if (page == l.pages - 1 && k >= count - std::min(rare, onLast)) return l.perPage - (count - k);
    return k % l.perPage;
}

struct CardRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool has(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// The strip's height, and a cell's rectangle with the strip's top left at (x0, y0).
inline float cardStripHeight(const CardLayout& l) { return static_cast<float>(l.rows) * l.height + static_cast<float>(l.rows - 1) * kCardGap; }
inline CardRect cardCellRect(const CardLayout& l, int cell, float x0, float y0) {
    const int columns = std::max(1, l.columns);
    return {x0 + static_cast<float>(cell % columns) * (l.width + kCardGap), y0 + static_cast<float>(cell / columns) * (l.height + kCardGap), l.width, l.height};
}
// The "+N more" cell of a paged strip: the last cell. Its left end is the arrow back, its right end the arrow on, and
// the middle turns to the next page (round to the first after the last), so a press anywhere on it does something.
inline CardRect cardPagerRect(const CardLayout& l, float x0, float y0) { return cardCellRect(l, l.rows * l.columns - 1, x0, y0); }
// How many squads are on other pages than this one: the N of "+N more".
inline int cardsElsewhere(const CardLayout& l, int count, int page) {
    return l.paged ? count - std::clamp(count - page * l.perPage, 0, l.perPage) : 0;
}

struct CardHit {
    enum What { None, Card, Back, On, More } what = None;
    int card = -1;  // Card: which, in order
};
inline CardHit cardAt(const CardLayout& l, int count, int rare, int page, float x0, float y0, float px, float py) {
    if (l.paged) {
        if (const CardRect r = cardPagerRect(l, x0, y0); r.has(px, py))
            return {px < r.x + kCardPagerArrow ? CardHit::Back : px >= r.x + r.w - kCardPagerArrow ? CardHit::On : CardHit::More, -1};
    }
    for (int k = 0; k < count; ++k) {
        const int cell = cardCell(l, count, rare, page, k);
        if (cell >= 0 && cardCellRect(l, cell, x0, y0).has(px, py)) return {CardHit::Card, k};
    }
    return {};
}
// The page after a press on the pager: Back and On stop at the ends, More goes round.
inline int cardPageAfter(const CardLayout& l, int page, CardHit::What what) {
    const int last = std::max(0, l.pages - 1);
    if (what == CardHit::Back) return std::max(0, page - 1);
    if (what == CardHit::On) return std::min(last, page + 1);
    if (what == CardHit::More) return page >= last ? 0 : page + 1;
    return std::clamp(page, 0, last);
}

}  // namespace illyria::game
