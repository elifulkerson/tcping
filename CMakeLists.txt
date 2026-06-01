# Build system for tcping. Replaces the Visual Studio solution (master.sln) and
# the nmake Makefile. One description builds both 32- and 64-bit: choose the
# architecture at configure time with the generator platform, e.g.
#
#   cmake -B build -A x64      # 64-bit  (was tcping-64)
#   cmake -B build -A Win32    # 32-bit  (was tcping-32)
#   cmake --build build --config Release
#
# Cross-platform (Linux/macOS) support is wired up below but the sources do not
# yet compile off Windows; that arrives with the Phase 3 compatibility layer
# described in MODERNIZATION.md.

cmake_minimum_required(VERSION 3.15)

# Required so CMAKE_MSVC_RUNTIME_LIBRARY (static CRT selection) is honored.
if (POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(tcping
  VERSION 0.39
  DESCRIPTION "A TCP probe utility that 'pings' a host by timing a TCP handshake"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized build for single-config generators (Ninja, Makefiles).
# Multi-config generators (Visual Studio) pick the config at build time instead.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Link the CRT statically so the .exe runs without the Visual C++ redistributable,
# matching the original /MT Release build (the "still works on XP" note in make.bat).
if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(tcping
  tcping-src/base64.cpp
  tcping-src/main.cpp
  tcping-src/tcping.cpp
  tcping-src/tee.cpp
  tcping-src/ws-util.cpp)

if (WIN32)
  target_link_libraries(tcping PRIVATE ws2_32)
  if (MSVC)
    # The original Visual Studio project built with the Unicode character set.
    target_compile_definitions(tcping PRIVATE UNICODE _UNICODE)
  endif()
else()
  message(WARNING
    "tcping currently targets Windows (Winsock) only. The CMake build is wired "
    "up and configures fine, but the sources will not compile on this platform "
    "until the cross-platform compatibility layer lands (Phase 3 in "
    "MODERNIZATION.md).")
endif()

install(TARGETS tcping RUNTIME DESTINATION bin)
