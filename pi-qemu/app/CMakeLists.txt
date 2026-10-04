cmake_minimum_required(VERSION 3.21)
project(pi-qemu LANGUAGES CXX)

# pi-qemu: runs a TriMixxx deck's SD card on an emulated Raspberry Pi 4, and is
# the deck's control surface -- a virtual S3 on the Pi's UART -- as a window and
# from the command line. See ../PLAN.md.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(pi-qemu
    src/main.cpp
    src/card.cpp
    src/firmware.cpp
    src/machine.cpp
    src/s3.cpp
    src/wiring.cpp
    src/control.cpp
    src/sticks.cpp
    src/deckwindow.cpp
)

# The S3 firmware's MIDI table is the single source of truth for every address:
# compile it in rather than copy it.
target_include_directories(pi-qemu PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../firmwares/trimixxx-midi/lib/PiLink)

# Where the repo is, so pi-qemu finds qemu/.build/bin and mixxx_config/units.
target_compile_definitions(pi-qemu PRIVATE
    PI_QEMU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_options(pi-qemu PRIVATE -Wall -Wextra)
target_link_libraries(pi-qemu PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
