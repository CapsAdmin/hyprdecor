cmake_minimum_required(VERSION 3.27)

project(hyprdecor
    DESCRIPTION "hyprdecor plugin for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SRC "src/*.cpp")

add_library(hyprdecor SHARED ${SRC})

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    libinput
    libudev
    librsvg-2.0
    pangocairo
    pixman-1
    wayland-server
    xkbcommon 
)
target_link_libraries(hyprdecor PRIVATE rt PkgConfig::deps)

install(TARGETS hyprdecor)
