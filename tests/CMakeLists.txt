add_executable(illyria_tests
    test_main.cpp
    test_fixed.cpp
    test_rng.cpp
    test_ballistics.cpp
    test_world.cpp
    test_terrain_combat.cpp
    test_combat.cpp
    test_game.cpp
    test_units.cpp
    test_region.cpp
    test_continent.cpp
    test_ground.cpp
    test_castle.cpp
    test_campaign.cpp
    test_figures.cpp
    test_detail_land.cpp
    test_campaign_quests.cpp
    test_campaign_sea.cpp
    test_campaign_ai.cpp
    test_paths.cpp
)
target_link_libraries(illyria_tests PRIVATE illyria_game illyria_figures)

if(EMSCRIPTEN)
    # Propagate main()'s return value as the node process exit code.
    target_link_options(illyria_tests PRIVATE -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
    add_test(NAME illyria_tests COMMAND node $<TARGET_FILE:illyria_tests>)
else()
    add_test(NAME illyria_tests COMMAND illyria_tests)
endif()
