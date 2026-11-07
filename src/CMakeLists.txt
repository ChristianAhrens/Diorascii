file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/assets/*.sprite" "${CMAKE_SOURCE_DIR}/assets/*.scene")
list(SORT ASSET_FILES)
string(REPLACE ";" "|" ASSET_ARG "${ASSET_FILES}")

set(EMBED_CPP "${CMAKE_CURRENT_BINARY_DIR}/embedded_assets.cpp")
add_custom_command(
  OUTPUT "${EMBED_CPP}"
  COMMAND ${CMAKE_COMMAND} "-DOUT=${EMBED_CPP}" "-DROOT=${CMAKE_SOURCE_DIR}/assets"
          "-DFILES=${ASSET_ARG}" -P "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
  DEPENDS ${ASSET_FILES} "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
  COMMENT "Embedding assets"
  VERBATIM)

configure_file(core/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h")

add_library(diorascii_core STATIC
  core/utf8.cpp
  core/frame.cpp
  core/sprite.cpp
  core/scene.cpp
  core/assets.cpp
  core/world.cpp
  "${EMBED_CPP}")
target_include_directories(diorascii_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
diorascii_warnings(diorascii_core)

if(WIN32)
  set(TERM_PLATFORM term/terminal_win.cpp)
else()
  set(TERM_PLATFORM term/terminal_posix.cpp)
endif()
add_library(diorascii_term STATIC term/vt.cpp ${TERM_PLATFORM})
target_link_libraries(diorascii_term PUBLIC diorascii_core)
diorascii_warnings(diorascii_term)

add_executable(diorascii app/main.cpp)
target_link_libraries(diorascii PRIVATE diorascii_core diorascii_term)
diorascii_warnings(diorascii)
