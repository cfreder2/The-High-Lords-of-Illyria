#pragma once

// The sprite atlas layout (docs/SPRITE_SPEC.md; tools/sprites/layout.py holds
// the same numbers for the art pipeline). A sheet's cell table lists, view by
// view, every frame of every clip, then the gib parts. Old sheets (26 frames
// a view: idle, walk, attack, flinch, one death) still load: the clips they
// lack fall back to ones they have. A sheet may instead list its own clips
// (sprite_clips.h: any clips, any frames a view, the gib parts after them). An old sheet may hold the gib parts
// too (150 cells: tools/sprites/sheet_gibs.py cuts them from its idle pictures).

#include <cstdint>

namespace illyria::game::sprites {

enum class Clip : uint8_t { Idle, Walk, Attack, Flinch, DieBack, DieFront, DieCrumple, DieBurn, Count };
constexpr int kClipCount = static_cast<int>(Clip::Count);
constexpr int kClipFrames[kClipCount] = {4, 8, 6, 2, 6, 6, 6, 6};
constexpr int kClipFirst[kClipCount] = {0, 4, 12, 18, 20, 26, 32, 38};
constexpr const char* kClipNames[kClipCount] = {"idle", "walk", "attack", "flinch", "die_back", "die_front", "die_crumple", "die_burn"};
constexpr int kViews = 5;
constexpr int kFramesPerView = 44;        // this layout
constexpr int kLegacyFramesPerView = 26;  // the old one (figures::kFramesPerView)

// Gib parts, two looks each, after the views' cells.
enum class Gib : uint8_t { Head, Torso, Arm, Leg, Chunk, Weapon, Shield, MountHead, MountLeg, Wing, Count };
constexpr int kGibCount = static_cast<int>(Gib::Count);
constexpr const char* kGibNames[kGibCount] = {"head", "torso", "arm", "leg", "chunk", "weapon", "shield", "mount_head", "mount_leg", "wing"};
constexpr int kGibVariants = 2;
constexpr int kCells = kViews * kFramesPerView + kGibCount * kGibVariants;  // 240
constexpr int kLegacyCells = kViews * kLegacyFramesPerView;                // 130
constexpr int kLegacyGibCells = kLegacyCells + kGibCount * kGibVariants;   // 150: an old sheet given its gib parts

// Seconds the deaths take (figures::kDieSeconds for the falls); burning is slower.
constexpr float kDieSeconds = 0.9f;
constexpr float kBurnSeconds = 1.5f;

// The clip a sheet holds in place of c: itself, or what it falls back to when
// the sheet (framesPerView frames a view) has no such clip.
constexpr Clip available(Clip c, int framesPerView) {
    while (kClipFirst[static_cast<int>(c)] + kClipFrames[static_cast<int>(c)] > framesPerView) {
        if (c == Clip::DieBurn) c = Clip::DieCrumple;
        else if (c == Clip::DieFront || c == Clip::DieCrumple) c = Clip::DieBack;
        else break;
    }
    return c;
}
// The frame number within a view of frame i of clip c, for a sheet of framesPerView.
constexpr int frameIndex(Clip c, int i, int framesPerView) {
    const Clip a = available(c, framesPerView);
    const int n = kClipFrames[static_cast<int>(a)];
    return kClipFirst[static_cast<int>(a)] + (i < 0 ? 0 : i >= n ? n - 1 : i);
}
constexpr int gibSlot(Gib g, int variant) {
    return kViews * kFramesPerView + static_cast<int>(g) * kGibVariants + (variant & 1);
}
// Where a gib slot (gibSlot) is in the cell table of a sheet of framesPerView
// frames a view: the gib parts follow its views' cells.
constexpr int kGibBase = kViews * kFramesPerView;
constexpr int gibCell(int slot, int framesPerView) { return slot - kGibBase + kViews * framesPerView; }

// The part drawn for one a sheet has no picture of, in turn (`step` 0 the part itself; Gib::Count past the last): a
// beast has no arms, so its second pair of limbs are its wings or its legs; a rider's mount gives the legs; what
// nothing else stands in for is a gobbet. So every part the sim throws is a piece of the soldier's own picture.
constexpr Gib gibStandIn(Gib g, int step) {
    constexpr Gib kNone = Gib::Count;
    constexpr Gib kChain[kGibCount][5] = {
        {Gib::Head, Gib::MountHead, Gib::Chunk, kNone, kNone},
        {Gib::Torso, Gib::Chunk, kNone, kNone, kNone},
        {Gib::Arm, Gib::Wing, Gib::MountLeg, Gib::Leg, Gib::Chunk},
        {Gib::Leg, Gib::MountLeg, Gib::Chunk, kNone, kNone},
        {Gib::Chunk, kNone, kNone, kNone, kNone},
        {Gib::Weapon, Gib::Chunk, kNone, kNone, kNone},
        {Gib::Shield, Gib::Chunk, kNone, kNone, kNone},
        {Gib::MountHead, Gib::Head, Gib::Chunk, kNone, kNone},
        {Gib::MountLeg, Gib::Leg, Gib::Chunk, kNone, kNone},
        {Gib::Wing, Gib::Arm, Gib::Chunk, kNone, kNone},
    };
    return step < 0 || step >= 5 || g >= Gib::Count ? kNone : kChain[static_cast<int>(g)][step];
}
// The slot (gibSlot) to draw part g with in a sheet where `has(slot)` says which it holds: the part's own look
// `variant`, else its other look, else what stands in for it; -1 when the sheet has none of them.
template <class Has>
constexpr int gibSlotFor(Gib g, int variant, Has&& has) {
    for (int step = 0; gibStandIn(g, step) != Gib::Count; ++step)
        for (int v = 0; v < kGibVariants; ++v)
            if (const int slot = gibSlot(gibStandIn(g, step), variant ^ v); has(slot)) return slot;
    return -1;
}
// What a body blown apart throws besides flesh, given out in this order to its first gobbets (the n-th gobbet takes
// the n-th of these its sheet holds; the rest stay gobbets): its weapon, its shield, its mount's head, a wing.
constexpr Gib kGibSpoils[] = {Gib::Weapon, Gib::Shield, Gib::MountHead, Gib::Wing};
template <class Has>
constexpr Gib gibSpoil(int n, Has&& has) {
    for (Gib g : kGibSpoils) {
        if (!has(gibSlot(g, 0)) && !has(gibSlot(g, 1))) continue;
        if (n-- == 0) return g;
    }
    return Gib::Chunk;
}
// The bones of the bloodless (the sim throws every piece of them as a bone): by a number of the piece's own, a
// skull or a ribcage now and then, arm and leg bones mostly.
constexpr Gib gibOfBone(uint32_t pick) {
    constexpr Gib kBones[8] = {Gib::Head, Gib::Torso, Gib::Arm, Gib::Leg, Gib::Arm, Gib::Leg, Gib::Arm, Gib::Leg};
    return kBones[pick % 8];
}

// The pre-rendered sheet a unit type (sim::UnitType's number) is drawn with:
// its own once the site's sprites/index.json lists it (`listed`), else a
// stand-in's until it is made (game/web/sheets.cpp fetches whichever). The
// hyena-folk (27) wear the light infantry's (0) until theirs is in the index.
constexpr int kHyenaType = 27;
constexpr int sheetStandIn(int type, bool listed = false) { return !listed && type == kHyenaType ? 0 : type; }

// How a soldier died, as the sim's death matrix records it (sim::DeathKind and
// sim::DamageType, same order, so a static_cast from those fits).
enum class DeathKind : uint8_t { None, Fell, Severed, Gibbed, Pierced, Charred };
enum class DamageType : uint8_t { Slash, Cleave, Blunt, Pierce, Explosive, Fire };
// The death clip to play: burned bodies writhe, limbs cut off crumple to
// their knees; the rest go the way the blow travelled: forward when struck
// from behind, else cut men crumple and the others fall back. along is the dot product of the
// blow's flat travel and the soldier's facing (+1 struck square in the back).
// An unknown death (None) plays the old fall.
constexpr Clip deathClip(DeathKind kind, DamageType cause, float along) {
    switch (kind) {
        case DeathKind::None: return Clip::DieBack;
        case DeathKind::Charred: return Clip::DieBurn;
        case DeathKind::Severed: return Clip::DieCrumple;
        default: break;
    }
    if (along > 0.25f) return Clip::DieFront;
    if (kind == DeathKind::Fell && (cause == DamageType::Slash || cause == DamageType::Cleave)) return Clip::DieCrumple;
    return Clip::DieBack;
}

}  // namespace illyria::game::sprites
