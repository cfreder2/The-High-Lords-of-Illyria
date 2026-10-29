#pragma once

// The sprite atlas layout (docs/SPRITE_SPEC.md; tools/sprites/layout.py holds
// the same numbers for the art pipeline). A sheet's cell table lists, view by
// view, every frame of every clip, then the gib parts. Old sheets (26 frames
// a view: idle, walk, attack, flinch, one death) still load: the clips they
// lack fall back to ones they have.

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
