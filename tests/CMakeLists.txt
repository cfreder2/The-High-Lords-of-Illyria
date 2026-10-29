# The voice manifest, built in so the tests can check it on every platform.
set(ILLYRIA_VOICE_MANIFEST_FILE "${CMAKE_SOURCE_DIR}/game/web/voice/lines.json")
file(READ "${ILLYRIA_VOICE_MANIFEST_FILE}" ILLYRIA_VOICE_MANIFEST)
configure_file(voice_manifest.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/voice_manifest.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ILLYRIA_VOICE_MANIFEST_FILE}")

# The sprite manifest's file list (tools/sprites/templates.py), for tests/test_sprite_layout.cpp.
set(ILLYRIA_SPRITE_FILES_FILE "${CMAKE_SOURCE_DIR}/docs/sprite_templates/files.txt")
file(READ "${ILLYRIA_SPRITE_FILES_FILE}" ILLYRIA_SPRITE_FILES)
configure_file(sprite_files.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/sprite_files.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ILLYRIA_SPRITE_FILES_FILE}")

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
    test_walls.cpp
    test_campaign.cpp
    test_figures.cpp
    test_detail_land.cpp
    test_campaign_quests.cpp
    test_campaign_sea.cpp
    test_campaign_ai.cpp
    test_campaign_rare.cpp
    test_paths.cpp
    test_command.cpp
    test_orders.cpp
    test_morale_rally.cpp
    test_voice.cpp
    test_sprite_layout.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/sprite_files.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/voice_manifest.cpp
)
target_link_libraries(illyria_tests PRIVATE illyria_game illyria_figures)

if(EMSCRIPTEN)
    # Propagate main()'s return value as the node process exit code.
    target_link_options(illyria_tests PRIVATE -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
    add_test(NAME illyria_tests COMMAND node $<TARGET_FILE:illyria_tests>)
else()
    add_test(NAME illyria_tests COMMAND illyria_tests)
endif()
# The manifest is one long string; every compiler here takes it.
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/voice_manifest.cpp" "${CMAKE_CURRENT_BINARY_DIR}/sprite_files.cpp" PROPERTIES COMPILE_OPTIONS -Wno-overlength-strings)
