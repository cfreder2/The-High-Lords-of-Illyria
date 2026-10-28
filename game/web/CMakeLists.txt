# The browser build: produces illyria.js + illyria.wasm next to index.html.
set(ILLYRIA_VERSION "0.0.0-dev" CACHE STRING "Version shown in the game")

add_executable(illyria_web
    main.cpp
    sheets.cpp
    ../render/renderer.cpp
    ../render/renderer_detail.cpp
    ../render/renderer_sheets.cpp
    ../render/renderer_orders.cpp
    ../render/meshes.cpp
)
set_target_properties(illyria_web PROPERTIES OUTPUT_NAME illyria SUFFIX ".js")
target_link_libraries(illyria_web PRIVATE illyria_game illyria_figures)
target_compile_definitions(illyria_web PRIVATE ILLYRIA_VERSION="${ILLYRIA_VERSION}")
target_compile_options(illyria_web PRIVATE -Wno-conversion)
target_link_options(illyria_web PRIVATE
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1
    -sENVIRONMENT=web
    "-sEXPORTED_FUNCTIONS=['_main','_illyria_version','_illyria_pointer','_illyria_wheel','_illyria_select_squad','_illyria_focus_squad','_illyria_stop','_illyria_charge','_illyria_hot_spot','_illyria_toggle_pause','_illyria_cycle_formation','_illyria_cycle_speed','_illyria_restart','_illyria_fast_forward','_illyria_set_camera','_illyria_finish_detail','_illyria_overview','_illyria_set_graphics','_illyria_graphics_preset','_illyria_hud','_illyria_labels','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['cwrap','ccall','UTF8ToString']"
)

# Assemble a deployable site in <build>/site.
set(ILLYRIA_SITE_DIR "${CMAKE_BINARY_DIR}/site")
configure_file(index.html "${ILLYRIA_SITE_DIR}/index.html" @ONLY)
configure_file(manifest.webmanifest "${ILLYRIA_SITE_DIR}/manifest.webmanifest" @ONLY)
configure_file(icon.svg "${ILLYRIA_SITE_DIR}/icon.svg" COPYONLY)
configure_file(voice.js "${ILLYRIA_SITE_DIR}/voice.js" COPYONLY)
add_custom_command(TARGET illyria_web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE_DIR:illyria_web>/illyria.js $<TARGET_FILE_DIR:illyria_web>/illyria.wasm "${ILLYRIA_SITE_DIR}/"
)
# Pre-rendered sprite sheets (tools/blender), fetched as battles need them.
add_custom_target(illyria_web_sprites ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/sprites" "${ILLYRIA_SITE_DIR}/sprites"
)
# The voices: the manifest of lines and whatever has been recorded (tools/voice).
add_custom_target(illyria_web_voice ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/voice" "${ILLYRIA_SITE_DIR}/voice"
)
