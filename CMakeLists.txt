cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Canary3DS LANGUAGES CXX VERSION 1.0.0)

include(cmake/CPM.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUSFILE REQUIRED opusfile)

CPMAddPackage(
  URI     "gh:kynex7510/RIP#93c244d"
  OPTIONS "RIP_BACKEND citro3d"
)

add_executable(${PROJECT_NAME}
  source/audio/audio_engine.cpp
  source/audio/audio_decoder.cpp
  source/audio/opus_decoder.cpp
  source/audio/mp3_decoder.cpp
  source/audio/flac_decoder.cpp
  source/audio/vorbis_decoder.cpp
  source/audio/wav_decoder.cpp

  source/main.cpp
  source/stb_image.cpp
  source/filebrowser.cpp
  source/gfx.cpp
  source/image.cpp
  source/base64.cpp
  source/playlist.cpp
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -g -O2 -Wall
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/
  $ENV{DEVKITPRO}/libctru/include
  $ENV{DEVKITPRO}/portlibs/3ds/include
  $ENV{DEVKITPRO}/devkitARM/arm-none-eabi/include
  $ENV{DEVKITPRO}/devkitARM/arm-none-eabi/include/c++/15.1.0
  $ENV{DEVKITPRO}/devkitARM/arm-none-eabi/include/c++/15.1.0/arm-none-eabi
  ${OPUSFILE_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  mpg123 FLAC vorbisidec ogg rip citro2d citro3d ctru m ${OPUSFILE_LIBRARIES}
)

ctr_generate_smdh(${PROJECT_NAME}.smdh
  NAME "${PROJECT_NAME}"
  DESCRIPTION "v${PROJECT_VERSION}"
  AUTHOR "Canary3DS Team"
)

ctr_create_3dsx(${PROJECT_NAME}
  SMDH ${PROJECT_NAME}.smdh
  ROMFS romfs
)

# CIA BUILD
set(CIA_FILE ${PROJECT_NAME}.cia)
set(BANNER_BIN ${PROJECT_NAME}.bnr)
set(ELF_FILE ${PROJECT_NAME}.elf)

set(APP_UNIQUE_ID 0xCA4A5)

# Build banner
add_custom_command(
  OUTPUT ${BANNER_BIN}
  COMMAND bannertool makebanner
    -i ${CMAKE_SOURCE_DIR}/assets/banner.png
    -a ${CMAKE_SOURCE_DIR}/assets/banner.wav
    -o ${BANNER_BIN}
  DEPENDS ${PROJECT_NAME}.smdh
  COMMENT "Building banner"
)

# Build CIA
add_custom_command(
  OUTPUT ${CIA_FILE}
  COMMAND makerom
    -f cia
    -o ${CIA_FILE}
    -elf ${ELF_FILE}
    -rsf ${CMAKE_SOURCE_DIR}/assets/canary3ds.rsf
    -icon ${PROJECT_NAME}.smdh
    -banner ${BANNER_BIN}
    -target t
    -exefslogo
    -DAPP_TITLE="${PROJECT_NAME}"
    -DAPP_PRODUCT_CODE=CTR-H-CANR
    -DAPP_UNIQUE_ID=${APP_UNIQUE_ID}
  DEPENDS ${PROJECT_NAME} ${BANNER_BIN}
  COMMENT "Building CIA"
)

# Add target (builds automatically with ALL)
add_custom_target(${PROJECT_NAME}_cia ALL
  DEPENDS ${CIA_FILE}
)
