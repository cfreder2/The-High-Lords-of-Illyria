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
    Count
};

inline constexpr int kUnitTypeCount = static_cast<int>(UnitType::Count);

UnitStats unitStats(UnitType type);
const char* unitTypeName(UnitType type);

}  // namespace illyria::sim
