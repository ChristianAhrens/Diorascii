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
  core/menu.cpp
  core/assets.cpp
  core/world.cpp
  core/ambience.cpp
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

# Ambient sound. The vendored miniaudio is its own library, built without our warnings.
add_library(diorascii_miniaudio STATIC audio/miniaudio_impl.cpp)
target_include_directories(diorascii_miniaudio SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/third_party/miniaudio")
if(APPLE)
  target_link_libraries(diorascii_miniaudio PUBLIC "-framework CoreFoundation"
    "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
  target_link_libraries(diorascii_miniaudio PUBLIC ${CMAKE_DL_LIBS} pthread m)
endif()

add_library(diorascii_audio STATIC audio/audio.cpp)
target_link_libraries(diorascii_audio PUBLIC diorascii_core PRIVATE diorascii_miniaudio)
diorascii_warnings(diorascii_audio)

# The interactive state (scene, menu, style, time of day, toasts), shared by every frontend.
add_library(diorascii_app STATIC app/session.cpp)
target_link_libraries(diorascii_app PUBLIC diorascii_core)
diorascii_warnings(diorascii_app)

add_executable(diorascii app/main.cpp)
target_link_libraries(diorascii PRIVATE diorascii_app diorascii_core diorascii_term diorascii_audio)
diorascii_warnings(diorascii)
