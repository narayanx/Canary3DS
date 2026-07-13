cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Canary LANGUAGES CXX VERSION 1.2.0)

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
  source/audio/audio_dsp.cpp
  source/audio/opus_decoder.cpp
  source/audio/mp3_decoder.cpp
  source/audio/flac_decoder.cpp
  source/audio/vorbis_decoder.cpp
  source/audio/wav_decoder.cpp
  source/audio/aac_decoder.cpp

  source/main.cpp
  source/app_lifecycle.cpp
  source/input_handlers.cpp
  source/render_frame.cpp
  source/filebrowser.cpp
  source/gfx.cpp
  source/image.cpp
  source/playlist.cpp
  source/scrobbler.cpp
  source/settings.cpp
  source/toggle_settings.cpp
)

target_sources(${PROJECT_NAME} PRIVATE
  external/stb/stb_image.cpp
  external/base64/base64.cpp
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -g -O2 -Wall
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/
  
  ${CMAKE_SOURCE_DIR}/external/stb/
  ${CMAKE_SOURCE_DIR}/external/base64/
  ${CMAKE_SOURCE_DIR}/external/dr_wav/
  ${CMAKE_SOURCE_DIR}/external/ffmpeg/

  $ENV{DEVKITPRO}/libctru/include
  $ENV{DEVKITPRO}/portlibs/3ds/include
  $ENV{DEVKITPRO}/devkitARM/arm-none-eabi/include
  $ENV{DEVKITPRO}/devkitARM/arm-none-eabi/include/c++/15.1.0
  $ENV{DEVKITPRO}/devkitARM/arm-none-eabi/include/c++/15.1.0/arm-none-eabi
  ${OPUSFILE_INCLUDE_DIRS}
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/external/ffmpeg/lib
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  # aac decoding
  avformat
  avcodec
  avutil
  swresample

  # Other audio decoders
  mpg123
  FLAC
  vorbisidec
  ogg

  # Cover art (convert images to 3ds format)
  rip

  citro2d
  citro3d
  ctru
  m

  # opus decoding
  ${OPUSFILE_LIBRARIES}
)

ctr_generate_smdh(${PROJECT_NAME}.smdh
  NAME "${PROJECT_NAME}"
  DESCRIPTION "v${PROJECT_VERSION}"
  AUTHOR "Canary Team"
  ICON "${CMAKE_SOURCE_DIR}/assets/icon.png"
  SMALL_ICON "${CMAKE_SOURCE_DIR}/assets/icon_small.png"
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

# -ver expects a 16-bit integer, not a string
math(EXPR CIA_VERSION
    "(${PROJECT_VERSION_MAJOR} << 10) |
     (${PROJECT_VERSION_MINOR} << 4)  |
      ${PROJECT_VERSION_PATCH}")

# Build CIA
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/${CIA_FILE}
  COMMAND makerom
    -f cia
    -ver ${CIA_VERSION}
    -o ${CMAKE_BINARY_DIR}/${CIA_FILE}
    -elf ${CMAKE_BINARY_DIR}/${ELF_FILE}
    -rsf ${CMAKE_SOURCE_DIR}/assets/canary.rsf
    -icon ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.smdh
    -banner ${CMAKE_BINARY_DIR}/${BANNER_BIN}
    -target t
    -exefslogo
    -DROMFS_DIR="${CMAKE_SOURCE_DIR}/romfs"
    -DAPP_TITLE="${PROJECT_NAME}"
    -DAPP_PRODUCT_CODE=CTR-H-CANR
    -DAPP_UNIQUE_ID=${APP_UNIQUE_ID}
  DEPENDS ${PROJECT_NAME} ${BANNER_BIN}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Building CIA"
)

# Add target (builds automatically with ALL)
add_custom_target(${PROJECT_NAME}_cia ALL
  DEPENDS ${CMAKE_BINARY_DIR}/${CIA_FILE}
)
