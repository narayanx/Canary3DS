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
  source/main.cpp
  source/stb_image.cpp
  source/filebrowser.cpp
  source/gfx.cpp
  source/image.cpp
  source/opus.cpp
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
  rip citro2d citro3d ctru m ${OPUSFILE_LIBRARIES}
)

ctr_generate_smdh(${PROJECT_NAME}.smdh
  NAME "${PROJECT_NAME}"
  DESCRIPTION "v${PROJECT_VERSION}"
  AUTHOR "Narayan"
)

ctr_create_3dsx(${PROJECT_NAME}
  SMDH ${PROJECT_NAME}.smdh
  ROMFS romfs
)
