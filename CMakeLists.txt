cmake_minimum_required(VERSION 3.21)

# The version comes from the latest released section of CHANGELOG.md ("## [1.2.3] date"), the
# single place it is written down. CMake reconfigures when the file changes.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" DIORASCII_CHANGELOG_HEADINGS
     REGEX "^## \\[[0-9]+\\.[0-9]+\\.[0-9]+\\]")
if(NOT DIORASCII_CHANGELOG_HEADINGS)
  message(FATAL_ERROR "CHANGELOG.md has no released version section like '## [1.0.0] 2026-11-07'")
endif()
list(GET DIORASCII_CHANGELOG_HEADINGS 0 DIORASCII_CHANGELOG_LATEST)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" DIORASCII_CHANGELOG_VERSION "${DIORASCII_CHANGELOG_LATEST}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md")

# tvOS apps target tvOS 17 and later (must be set before project()).
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 17.0 CACHE STRING "")
endif()

# One tvOS project for both the device and the simulator, so Xcode's run destinations list the
# Apple TV simulators as well (without this only devices are offered).
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  set(CMAKE_XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "appletvos appletvsimulator")
  # Only the app gets a scheme (enabled in apple/tvOS): otherwise Xcode opens on ALL_BUILD, which
  # has nothing to run, so Run would build and then do nothing.
  set(CMAKE_XCODE_GENERATE_SCHEME OFF)
  set(CMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL YES)
endif()

project(diorascii VERSION ${DIORASCII_CHANGELOG_VERSION} LANGUAGES CXX)
if(CMAKE_SYSTEM_NAME MATCHES "^(tvOS|iOS)$")
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# The version reported by --version and used for package names is DIORASCII_VERSION_STRING.
# Release builds pass the pushed tag as -DDIORASCII_VERSION=x.y.z (the release workflow has
# checked it against the changelog). Without it the version is the changelog's, and a build that
# is not exactly that release's tagged commit is marked "<version>-dev+<commit>", so a binary
# built from develop never passes for the release.
set(DIORASCII_VERSION "" CACHE STRING "Version string compiled into the binary (default: from CHANGELOG.md)")
if(DIORASCII_VERSION)
  set(DIORASCII_VERSION_STRING "${DIORASCII_VERSION}")
else()
  set(DIORASCII_VERSION_STRING "${PROJECT_VERSION}")
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE DIORASCII_GIT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE DIORASCII_GIT_TAG_RESULT ERROR_QUIET)
    if(NOT DIORASCII_GIT_TAG_RESULT EQUAL 0 OR NOT DIORASCII_GIT_TAG STREQUAL PROJECT_VERSION)
      execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                      OUTPUT_VARIABLE DIORASCII_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      set(DIORASCII_VERSION_STRING "${PROJECT_VERSION}-dev")
      if(DIORASCII_GIT_COMMIT)
        set(DIORASCII_VERSION_STRING "${DIORASCII_VERSION_STRING}+${DIORASCII_GIT_COMMIT}")
      endif()
    endif()
  endif()
endif()
message(STATUS "Diorascii version ${DIORASCII_VERSION_STRING}")

option(DIORASCII_BUILD_TESTS "Build the test suite" ON)
# The terminal program and its tests; off on tvOS and iOS, which have no terminal.
if(CMAKE_SYSTEM_NAME MATCHES "^(tvOS|iOS)$")
  set(DIORASCII_TERMINAL_DEFAULT OFF)
else()
  set(DIORASCII_TERMINAL_DEFAULT ON)
endif()
option(DIORASCII_TERMINAL "Build the terminal program" ${DIORASCII_TERMINAL_DEFAULT})
option(DIORASCII_STATIC_RUNTIME "Link the C/C++ runtime statically (single-file binary)" ON)

if(DIORASCII_STATIC_RUNTIME)
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # The static C++ runtime is a separate package on some distributions (Fedora:
    # libstdc++-static). Use it only if it links, otherwise fall back to the shared one.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LINK_OPTIONS -static-libstdc++ -static-libgcc)
    check_cxx_source_compiles("int main() { return 0; }" DIORASCII_CAN_LINK_STATIC_LIBSTDCXX)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(DIORASCII_CAN_LINK_STATIC_LIBSTDCXX)
      add_link_options(-static-libstdc++ -static-libgcc)
    else()
      message(STATUS "Static libstdc++ not found, linking the C++ runtime dynamically "
                     "(Fedora: dnf install libstdc++-static for a single-file binary)")
    endif()
  endif()
endif()

function(diorascii_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # GCC notes how std::pair<double, double> is passed (an ABI change in GCC 10); not actionable.
      target_compile_options(${target} PRIVATE -Wno-psabi)
    endif()
  endif()
endfunction()

add_subdirectory(src)
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  add_subdirectory(apple/tvOS)
endif()

if(DIORASCII_BUILD_TESTS AND DIORASCII_TERMINAL)
  enable_testing()
  add_subdirectory(tests)
endif()

# `cmake --build <dir> --target docs` renders the API documentation when Doxygen is installed.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E env DIORASCII_VERSION=${DIORASCII_VERSION_STRING} ${DOXYGEN_EXECUTABLE} Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)
endif()

if(DIORASCII_TERMINAL)
  install(TARGETS diorascii RUNTIME DESTINATION bin)
endif()
install(FILES LICENSE README.md DESTINATION share/doc/diorascii)

set(CPACK_PACKAGE_NAME diorascii)
set(CPACK_PACKAGE_VERSION ${DIORASCII_VERSION_STRING})
set(CPACK_PACKAGE_VENDOR "Diorascii contributors")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
