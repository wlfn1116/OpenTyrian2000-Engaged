# iOS build. Produces an unsigned OpenTyrian2000.app; ios/README.md covers turning that
# into an installable .ipa. SDL2 and SDL2_net sources are expected under ios/external.
cmake_minimum_required(VERSION 3.21)

# SDL2's iOS backend is Objective-C and its HIDAPI is C++. Enabling both here rather than
# letting the subproject do it keeps the toolchain probes at the top level.
project(OpenTyrian2000 C CXX OBJC)

set(CMAKE_C_STANDARD 99)

set(OT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(OT_BUNDLE_ID "net.opentyrian.engaged" CACHE STRING "iOS bundle identifier")

# Version string for the bundle, taken from the header the title screen reads.
file(READ "${OT_ROOT}/src/opentyrian_version.h" OT_VERSION_HEADER)
if(OT_VERSION_HEADER MATCHES "OPENTYRIAN_VERSION[ \t]+\"[^\"0-9]*([0-9]+(\\.[0-9]+)*)\"")
  set(OT_VERSION "${CMAKE_MATCH_1}")
else()
  set(OT_VERSION "1.0")
endif()

# SDL2, built static so the app bundle needs no embedded frameworks.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2" sdl2 EXCLUDE_FROM_ALL)

# SDL2_net is four C files. Compiling them into the app avoids exporting an SDL2 package
# config just so its CMake project can find_package() the copy we already built.
set(SDL_NET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2_net")
set(SDL_NET_SOURCES
  "${SDL_NET_DIR}/SDLnet.c"
  "${SDL_NET_DIR}/SDLnetTCP.c"
  "${SDL_NET_DIR}/SDLnetUDP.c"
  "${SDL_NET_DIR}/SDLnetselect.c"
)

file(GLOB OT_SOURCES "${OT_ROOT}/src/*.c")

# Read-only game data, copied into OpenTyrian2000.app/data where SDL_GetBasePath finds it.
file(GLOB OT_DATA "${OT_ROOT}/data/*")
if(NOT OT_DATA)
  message(FATAL_ERROR "No files in ${OT_ROOT}/data; install the Tyrian 2000 data before configuring.")
endif()
set_source_files_properties(${OT_DATA} PROPERTIES MACOSX_PACKAGE_LOCATION "data")

add_executable(OpenTyrian2000 MACOSX_BUNDLE ${OT_SOURCES} ${SDL_NET_SOURCES} ${OT_DATA})

target_include_directories(OpenTyrian2000 PRIVATE "${OT_ROOT}/src" "${SDL_NET_DIR}")

# TARGET_IOS selects the mobile platform layer; the float and char flags keep this build's
# simulation bit-identical to the desktop and console targets for netplay and replays.
target_compile_definitions(OpenTyrian2000 PRIVATE
  NDEBUG
  TARGET_UNIX
  TARGET_IOS
  WITH_NETWORK
  TYRIAN_DIR="data"
)
target_compile_options(OpenTyrian2000 PRIVATE -fsigned-char -ffp-contract=off -Wall
                       -Wno-format-truncation -Wno-missing-field-initializers)

if(DEFINED OPENTYRIAN_COMMIT AND NOT OPENTYRIAN_COMMIT STREQUAL "")
  target_compile_definitions(OpenTyrian2000 PRIVATE OPENTYRIAN_COMMIT="${OPENTYRIAN_COMMIT}")
endif()

target_link_libraries(OpenTyrian2000 PRIVATE SDL2::SDL2-static SDL2::SDL2main)

set_target_properties(OpenTyrian2000 PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "OpenTyrian 2000"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${OT_BUNDLE_ID}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${OT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${OT_VERSION}"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OT_BUNDLE_ID}"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
)
