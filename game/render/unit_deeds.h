#pragma once

// What the renderer reads off a battle, read-only, for the clips a soldier
// plays on a sheet that lists its own (sprite_clips.h): which sides have won,
// and whether a missile soldier will loose when his reload ends. Nothing here
// touches the simulation or its hashed state.

#include <cstddef>
#include <cstdint>

#include "illyria/game/battle.h"

namespace illyria::game::deeds {

// The sides that have won (a bit per sim::PlayerId), 0 while the battle is
// fought or after a draw. Battle::outcome says whether the side at this
// screen won; capture the flag knows its winner, Horde its holders and its
// horde; a field battle is the player's side against the rest (the town's
// taker, when its capture point fell to him).
uint32_t victors(const Battle& b);

// Soldier i (an index into the world's units) is set to loose a missile
// when his reload ends: a missile soldier (not a scavenger), alive, unbroken,
// on the ground, standing still and out of melee, with a mark in range: the
// soldier or the ground he was sent at, else (left to himself, not marching)
// the nearest foe, beyond the reach he would close to melee in and within
// his own range. As sim::World::updateUnit picks his mark, read from outside.
bool willLoose(const Battle& b, size_t i);

}  // namespace illyria::game::deeds
