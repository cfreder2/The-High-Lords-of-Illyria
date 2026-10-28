# Deterministic gameplay simulation. Must not depend on any engine, renderer,
# platform API or floating-point math: every peer has to produce bit-identical
# state from the same inputs (see docs/GAME_DESIGN.md §5).
add_library(illyria_sim
    src/fixed.cpp
    src/ballistics.cpp
    src/world.cpp
    src/world_gates.cpp
    src/scenario.cpp
    src/terrain.cpp
    src/units.cpp
    src/region.cpp
    src/nav.cpp
    src/gait.cpp
    src/continent.cpp
    src/campaign.cpp
    src/campaign_rules.cpp
    src/campaign_ai.cpp
    src/campaign_quests.cpp
    src/campaign_sea.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/relief_data.cpp
)

# The continent's baked relief (tools/terrainbake), compiled in as data.
set(ILLYRIA_RELIEF ${CMAKE_CURRENT_SOURCE_DIR}/data/illyria_relief.bin)
set(ILLYRIA_RELIEF_DEPENDS "")
if(EXISTS ${ILLYRIA_RELIEF})
    set(ILLYRIA_RELIEF_DEPENDS ${ILLYRIA_RELIEF})
endif()
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/relief_data.cpp
    COMMAND ${CMAKE_COMMAND} -DIN=${ILLYRIA_RELIEF} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/relief_data.cpp -DNAME=kRelief
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake ${ILLYRIA_RELIEF_DEPENDS}
    COMMENT "Embedding the baked relief")
target_include_directories(illyria_sim PUBLIC include)
