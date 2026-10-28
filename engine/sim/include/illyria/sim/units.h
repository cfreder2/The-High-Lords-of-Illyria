#pragma once

#include <cstdint>

#include "illyria/sim/world.h"

namespace illyria::sim {

// Every unit type in the game. The Warlords roster (regular armies and
// allies, with Warlords II's terrain bonuses) plus the Myth units.
enum class UnitType : uint8_t {
    // Warlords: regular armies
    LightInfantry,
    HeavyInfantry,
    Archers,
    Pikemen,
    LightCavalry,
    HeavyCavalry,
    Dwarves,
    WolfRiders,
    Giants,
    Minotaurs,
    Spiders,
    Griffins,
    Pegasi,
    Elephants,
    Unicorns,
    // Warlords: allies
    Wizards,
    Dragons,
    Devils,
    Demons,
    Undead,
    Archons,
    Hero,
    // Myth
    DwarfGrenadier,
    Journeyman,
    Warrior,
    Berserker,
    // Siege engines
    Catapult,
    Count
};

inline constexpr int kUnitTypeCount = static_cast<int>(UnitType::Count);

UnitStats unitStats(UnitType type);
// How hard morale shocks shake this type, in percent of a man's; 0 never breaks.
int32_t moraleShockPct(UnitType type);
// Strength on the Warlords scale of 1 to 9 for a fresh unit of this type:
// what the campaign map and the HUD show (ranks, levels and blessings add to it).
int warlordsStrength(UnitType type);
const char* unitTypeName(UnitType type);
// How this type gets over the ground (gait.h).
Gait gaitOf(UnitType type);

}  // namespace illyria::sim
