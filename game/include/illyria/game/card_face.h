#pragma once

// What a squad's card shows and where (game/native/hud.cpp draws it; card_layout.h places the cards). A card is three
// lines, the same three on the full card (62 points) and the compact one (38), and each mark has ONE named slot, so a
// new mark is a field in the state JSON's squad (illyria_hud, game/app/app.cpp) and nothing moves:
//
//   +--------------------------------------+
//   | NAME (1 line; 2 on the full card) [COUNTER] |   top:    name left, the counter slot right
//   | 12/16 [tag]                [STATUS]  |   middle: count left, then the ground tag, the status slot right
//   | ======== strength bar ====== [PIPS]  |   bottom: the bar, the pips row at its right end
//   +--------------------------------------+
//
//   STATUS slot  one word, the most urgent of (cardStatus):
//                  broken     "rallyIn" != 0   a white flag and the seconds to its rally ("--" while a foe is on it)
//                  lit        "lit"            FUSE LIT (a man of it runs with a satchel's fuse burning)
//                  calling    "calling"        HELP   (it has called for help)
//                  answering  "answering"      ANSWER (it is on its way to one that called)
//                  hold       "hold"           HOLD
//                  idle       "idle"           IDLE
//   COUNTER slot what it has left to throw, as a pip and a number each:
//                  "sat" of "satMax"    satchels (dwarves; hyena-folk while one carries a satchel: an amber square)
//                  "ammo" of "ammoMax"  Molotovs, fireballs, any counted store (a red bar); a store over 24 (arrows)
//                                       is not shown until it is down to a quarter
//                  "holdFire"           two gold bars before them (a pause mark): it is holding its fire
//   PIPS row     "rank" 1-3 (regulars, veterans, elite): that many gold pips; recruits none
//   ground tag   "ground" ("high ground", "in the ford", "in woods"): its one word after the count (cardGroundTag);
//                it is the first thing to give way on a narrow card, and it never takes the status slot's room
//
// A field that is absent draws nothing and takes no room. The name is fitted (card_name.h) to the width the counter
// slot leaves it (CardFace::nameW), so no mark is ever over the name.

#include <algorithm>
#include <string>

namespace illyria::game {

struct CardFacts {
    int rallyIn = 0;  // broken: seconds to the rally, -1 while it cannot, 0 not broken
    bool calling = false, answering = false, hold = false, idle = false, holdFire = false;
    bool guarding = false, guarded = false;  // on guard duty (Command::Guard); a squad that someone guards
    bool lit = false;                         // a man of it runs with a satchel's fuse lit (the satchel run)
    int sat = 0, satMax = 0, ammo = 0, ammoMax = 0;
    int rank = 0;
};

enum class CardStatus { None, Broken, Lit, Calling, Answering, Guarding, Hold, Guarded, Idle };

inline CardStatus cardStatus(const CardFacts& f) {
    if (f.rallyIn != 0) return CardStatus::Broken;
    if (f.lit) return CardStatus::Lit;
    if (f.calling) return CardStatus::Calling;
    if (f.answering) return CardStatus::Answering;
    if (f.guarding) return CardStatus::Guarding;
    if (f.hold) return CardStatus::Hold;
    if (f.guarded) return CardStatus::Guarded;
    if (f.idle) return CardStatus::Idle;
    return CardStatus::None;
}
inline std::string cardStatusWord(const CardFacts& f) {
    switch (cardStatus(f)) {
        case CardStatus::Broken: return f.rallyIn > 0 ? std::to_string(f.rallyIn) + "s" : "--";
        case CardStatus::Lit: return "FUSE LIT";
        case CardStatus::Calling: return "HELP";
        case CardStatus::Answering: return "ANSWER";
        case CardStatus::Guarding: return "GUARD";
        case CardStatus::Hold: return "HOLD";
        case CardStatus::Guarded: return "GUARDED";
        case CardStatus::Idle: return "IDLE";
        case CardStatus::None: break;
    }
    return "";
}

// The counter slot's two numbers: shown at all, and what.
inline bool cardShowsSatchels(const CardFacts& f) { return f.satMax > 0; }
inline bool cardShowsAmmo(const CardFacts& f) { return f.ammoMax > 0 && (f.ammoMax <= 24 || f.ammo * 4 <= f.ammoMax); }
inline int cardRankPips(const CardFacts& f) { return std::clamp(f.rank, 0, 3); }

// "in the ford" -> "ford", "high ground" -> "high", "in woods" -> "woods": the one word that tells.
inline std::string cardGroundTag(std::string ground) {
    for (const char* lead : {"in the ", "in ", "on the ", "on "})
        if (ground.rfind(lead, 0) == 0) ground.erase(0, std::string(lead).size());
    if (const size_t sp = ground.find(' '); sp != std::string::npos) ground.erase(sp);
    return ground;
}

// Where each slot is in a card `cw` by `ch`, from the card's top left. `counterW` and `pipsW` are the widths the
// counter slot and the pips row need (0: nothing to show, and they take no room).
struct CardFace {
    float pad = 6;
    float nameX = 6, nameY = 4, nameW = 0, lineH = 12;
    int nameLines = 1;
    float counterX = 0, counterY = 0;  // the counter slot's left and top
    float rowY = 0;                    // the count's line (below the name's lines: add lineH a line past the first on the full card)
    float barX = 6, barY = 0, barW = 0, barH = 4;
    float pipsX = 0;                   // the pips row, on the bar's line
};
inline CardFace cardFace(float cw, float ch, bool compact, float counterW, float pipsW) {
    CardFace f;
    f.nameLines = compact ? 1 : 2;
    f.nameY = compact ? 4 : 5;
    f.nameW = cw - 2 * f.pad - (counterW > 0 ? counterW + 4 : 0);
    f.counterX = cw - f.pad - counterW;
    f.counterY = f.nameY + 1;
    f.rowY = compact ? 16 : 21;
    f.barH = compact ? 4 : 5;
    f.barY = ch - (compact ? 8 : 14);
    f.barW = cw - 2 * f.pad - (pipsW > 0 ? pipsW + 4 : 0);
    f.pipsX = cw - f.pad - pipsW;
    return f;
}

}  // namespace illyria::game
