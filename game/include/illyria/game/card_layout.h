#pragma once

// Where the squad cards of the desktop HUD go (game/native/hud.cpp): every squad gets a card, always. A card is
// 132 points wide; when the squads do not fit in one row they shrink a step or two (to a width the name still
// reads in), then the strip takes a second row and a third, and only past that does it page (arrows and the
// wheel over the strip), still never dropping a squad out of reach.

#include <algorithm>

namespace illyria::game {

struct CardLayout {
    float width = 132;    // one card
    int rows = 1;         // rows of cards in the strip
    int columns = 0;      // cards in a row
    int perPage = 0;      // cards on a page (all of them unless paged)
    int pages = 1;
    bool paged = false;   // the last cell of the last row holds the page arrows
};

constexpr float kCardGap = 6;
constexpr float kCardWidths[] = {132, 112, 96};  // the card's steps, widest first
constexpr int kCardMaxRows = 3;

// `count` cards in `avail` points of width (the screen less its margins).
inline CardLayout layoutCards(int count, float avail) {
    CardLayout out;
    count = std::max(count, 0);
    const auto columnsAt = [&](float width) { return std::max(1, static_cast<int>((avail + kCardGap) / (width + kCardGap))); };
    // Rows first, widest card first: one row of full cards, one of narrower, then two rows of full cards, and so on.
    for (int rows = 1; rows <= kCardMaxRows; ++rows)
        for (const float width : kCardWidths) {
            const int columns = columnsAt(width);
            if (columns * rows >= count) {
                out.width = width;
                out.rows = std::max(1, (count + columns - 1) / columns);  // (rows in use: no empty row's worth of space)
                out.columns = columns;  // (filled left to right, a row at a time, the last row part full)
                out.perPage = count;
                out.pages = 1;
                return out;
            }
        }
    // Too many even so: the narrowest card, the most rows, a page at a time; the last cell is the arrows.
    out.width = kCardWidths[sizeof kCardWidths / sizeof *kCardWidths - 1];
    out.rows = kCardMaxRows;
    out.columns = columnsAt(out.width);
    out.paged = true;
    out.perPage = std::max(1, out.rows * out.columns - 1);
    out.pages = (count + out.perPage - 1) / out.perPage;
    return out;
}

}  // namespace illyria::game
