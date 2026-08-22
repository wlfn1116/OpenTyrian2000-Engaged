# Unsigned iOS bundle. See ios/README.md for dependencies and IPA packaging.
cmake_minimum_required(VERSION 3.21)

# Enable Objective-C and C++ at the top level for SDL's iOS backend and HIDAPI.
# Use Ninja or Makefiles; Xcode runs every SDL feature probe in a separate xcodebuild.
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

# Compile SDL2_net into the app so its project does not need an exported SDL2 package.
set(SDL_NET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2_net")
set(SDL_NET_SOURCES
  "${SDL_NET_DIR}/SDLnet.c"
  "${SDL_NET_DIR}/SDLnetTCP.c"
  "${SDL_NET_DIR}/SDLnetUDP.c"
  "${SDL_NET_DIR}/SDLnetselect.c"
)

file(GLOB OT_SOURCES "${OT_ROOT}/src/*.c")

if(NOT EXISTS "${OT_ROOT}/data/tyrian1.lvl")
  message(FATAL_ERROR "No Tyrian 2000 data in ${OT_ROOT}/data; install it before configuring.")
endif()

add_executable(OpenTyrian2000 MACOSX_BUNDLE ${OT_SOURCES} ${SDL_NET_SOURCES})

# Copy data post-build; MACOSX_PACKAGE_LOCATION works only with the Xcode generator.
add_custom_command(TARGET OpenTyrian2000 POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${OT_ROOT}/data" "$<TARGET_BUNDLE_CONTENT_DIR:OpenTyrian2000>/data"
  COMMENT "Copying game data into the bundle")

# Compile the transparent normal, dark, and tinted icons into one asset catalog.
add_custom_command(TARGET OpenTyrian2000 POST_BUILD
  COMMAND xcrun actool "${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets"
          --compile "$<TARGET_BUNDLE_CONTENT_DIR:OpenTyrian2000>"
          --platform iphoneos
          --minimum-deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
          --target-device iphone --target-device ipad
          --app-icon AppIcon
          --output-partial-info-plist "${CMAKE_CURRENT_BINARY_DIR}/actool-partial.plist"
  # Non-Xcode builds must merge actool's icon keys into the bundle plist.
  COMMAND /usr/libexec/PlistBuddy
          -c "Merge ${CMAKE_CURRENT_BINARY_DIR}/actool-partial.plist"
          "$<TARGET_BUNDLE_CONTENT_DIR:OpenTyrian2000>/Info.plist"
  COMMENT "Compiling the icon asset catalog")

target_include_directories(OpenTyrian2000 PRIVATE "${OT_ROOT}/src" "${SDL_NET_DIR}")

# Keep iOS simulation compatible with the other netplay and replay targets.
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
  MACOSX_BUNDLE_BUNDLE_NAME "Tyrian 2000 Engaged"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${OT_BUNDLE_ID}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${OT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${OT_VERSION}"
)
