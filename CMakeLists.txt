cmake_minimum_required(VERSION 3.19)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} HYPRSHUTDOWN_VERSION)

add_compile_definitions(HYPRSHUTDOWN_VERSION="${HYPRSHUTDOWN_VERSION}")

project(
  hyprshutdown
  VERSION ${HYPRSHUTDOWN_VERSION}
  DESCRIPTION "A graceful shutdown utility for Hyprland ")

include(CTest)
include(CheckIncludeFile)
include(GNUInstallDirs)

set(PREFIX ${CMAKE_INSTALL_PREFIX})
set(INCLUDE ${CMAKE_INSTALL_FULL_INCLUDEDIR})
set(LIBDIR ${CMAKE_INSTALL_FULL_LIBDIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  deps
  REQUIRED
  IMPORTED_TARGET
  hyprtoolkit
  hyprutils>=0.11.0
  hyprlang
  pixman-1
  libdrm
)

find_package(glaze QUIET)
if (NOT glaze_FOUND)
    set(GLAZE_VERSION v6.1.0)
    message(STATUS "glaze dependency not found, retrieving ${GLAZE_VERSION} with FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze.git
        GIT_TAG ${GLAZE_VERSION}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glaze)
endif()

set(CMAKE_CXX_STANDARD 23)
add_compile_options(
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-unused-value
  -Wno-missing-field-initializers
  -Wpedantic)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
  message(STATUS "Configuring hyprshutdown in Debug")
  add_compile_definitions(HYPRSHUTDOWN_DEBUG)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring hyprshutdown in Release")
endif()

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp" "include/*.hpp")

add_executable(hyprshutdown ${SRCFILES})

target_link_libraries(hyprshutdown PkgConfig::deps glaze::glaze)

install(TARGETS hyprshutdown)