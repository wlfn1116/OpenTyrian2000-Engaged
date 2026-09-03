# PlayStation Vita build. Requires VitaSDK with the SDL2 package. The build
# wrapper assembles eboot.bin, LiveArea assets, and data into the final VPK.
cmake_minimum_required(VERSION 3.10)

# VitaSDK toolchain
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "VITASDK is not set in the environment. export VITASDK=<path to>vitasdk")
  endif()
endif()

project(OpenTyrian2000 C)

# vita.cmake provides the Vita packaging helpers.
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# VPK and LiveArea metadata. APP_VER is the zero-padded major.minor from
# src/opentyrian_version.h, matching build.ps1; the SFO requires "XX.YY", so
# input that does not fit that shape keeps the default.
set(VITA_APP_NAME "OpenTyrian 2000 Engaged")
set(VITA_TITLEID  "OTYR20000")
set(VITA_VERSION  "01.00")
file(READ "${CMAKE_SOURCE_DIR}/../src/opentyrian_version.h" OT_VERSION_HEADER)
if(OT_VERSION_HEADER MATCHES "OPENTYRIAN_VERSION[ \t]+\"[^\"0-9]*([0-9]+)\\.([0-9]+)")
  # Latch both captures first: the pad below overwrites CMAKE_MATCH_<n>.
  set(OT_VER_MAJOR "${CMAKE_MATCH_1}")
  set(OT_VER_MINOR "${CMAKE_MATCH_2}")
  string(REGEX REPLACE "^([0-9])$" "0\\1" OT_VER_MAJOR "${OT_VER_MAJOR}")
  string(REGEX REPLACE "^([0-9])$" "0\\1" OT_VER_MINOR "${OT_VER_MINOR}")
  if(OT_VER_MAJOR MATCHES "^[0-9][0-9]$" AND OT_VER_MINOR MATCHES "^[0-9][0-9]$")
    set(VITA_VERSION "${OT_VER_MAJOR}.${OT_VER_MINOR}")
  endif()
endif()

# Preserve engine assumptions, loader aliasing, and netplay float parity.
add_compile_options(-Wall -Wno-format-truncation -O2 -fsigned-char -fno-strict-aliasing
                    -ffp-contract=off)

# Platform-specific sources self-guard; the flat glob excludes the midiproc directory.
file(GLOB OT_SOURCES "${CMAKE_SOURCE_DIR}/../src/*.c")

add_executable(${PROJECT_NAME} ${OT_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_SOURCE_DIR}/../src"
  "$ENV{VITASDK}/arm-vita-eabi/include/SDL2"   # the engine includes "SDL.h" unqualified
)

# Match desktop Release assertions. TYRIAN_DIR is a fallback behind Vita runtime
# paths. vita_net.c supplies the SceNet-backed SDL2_net compatibility layer.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  NDEBUG
  TARGET_UNIX
  WITH_NETWORK
  TYRIAN_DIR="app0:data"
)

# Use an explicit commit id or derive one from Git; omit the title-screen line if empty.
if(NOT DEFINED OPENTYRIAN_COMMIT)
  execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}/.." rev-parse --short HEAD
                  OUTPUT_VARIABLE OPENTYRIAN_COMMIT
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
endif()
if(OPENTYRIAN_COMMIT)
  target_compile_definitions(${PROJECT_NAME} PRIVATE OPENTYRIAN_COMMIT="${OPENTYRIAN_COMMIT}")
endif()

# Static-link order follows sdl2.pc; SceNet and SceNetCtl serve vita_net.c.
target_link_libraries(${PROJECT_NAME}
  SDL2
  SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub SceMotion_stub SceHid_stub
  SceAudio_stub SceAudioIn_stub SceCommonDialog_stub SceSysmodule_stub
  SceAppMgr_stub SceAppUtil_stub ScePower_stub SceProcessmgr_stub SceIofilemgr_stub
  SceKernelThreadMgr_stub
  SceNet_stub SceNetCtl_stub
  m
)

# UNSAFE permits the game's ux0: reads and writes outside app0:.
vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
