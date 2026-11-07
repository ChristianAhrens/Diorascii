cmake_minimum_required(VERSION 3.21)
project(diorascii VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Reported by --version and used for package names. Release builds pass the pushed
# tag here (x.y.z); everyone else gets the version declared in project() above.
set(DIORASCII_VERSION "${PROJECT_VERSION}" CACHE STRING "Version string compiled into the binary")

option(DIORASCII_BUILD_TESTS "Build the test suite" ON)
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

if(DIORASCII_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS diorascii RUNTIME DESTINATION bin)
install(FILES LICENSE README.md DESTINATION share/doc/diorascii)

set(CPACK_PACKAGE_NAME diorascii)
set(CPACK_PACKAGE_VERSION ${DIORASCII_VERSION})
set(CPACK_PACKAGE_VENDOR "Diorascii contributors")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
