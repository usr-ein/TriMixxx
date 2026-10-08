cmake_minimum_required(VERSION 3.21)
project(pi-qemu LANGUAGES CXX)

# pi-qemu: the one tool for TriMixxx decks. It runs a deck's SD card on an
# emulated Raspberry Pi 4 with a virtual S3 on its UART, drives decks emulated
# or real, deploys the repo's code onto them, and builds their cards and
# releases. See ../README.md.
#
#   piqemu_core   everything but main(): src/<area>/, one directory each
#   pi-qemu       the command line (src/main.cpp)
#   tst_*         Qt Test, under tests/ (ctest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(FILTER CORE_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
add_library(piqemu_core STATIC ${CORE_SOURCES})

# The S3 firmware's MIDI table is the single source of truth for every address:
# compile it in rather than copy it.
target_include_directories(piqemu_core PUBLIC
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../firmwares/trimixxx-midi/lib/PiLink)

# The checkout this was built from: where pi-qemu looks when it runs outside
# any checkout (util/paths).
target_compile_definitions(piqemu_core PUBLIC
    PI_QEMU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_options(piqemu_core PUBLIC -Wall -Wextra)
target_link_libraries(piqemu_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

add_executable(pi-qemu src/main.cpp)
target_link_libraries(pi-qemu PRIVATE piqemu_core)
# pi-qemu's icon, for its windows and its Dock tile (when it shows one):
# icons/trimixxx.ico, rendered from icons/trimixxx.svg by icons/make-ico.py.
qt_add_resources(pi-qemu icons PREFIX / BASE icons FILES icons/trimixxx.ico)

enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/tst_*.cpp)
foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE piqemu_core Qt6::Test)
    target_compile_definitions(${test_name} PRIVATE PI_QEMU_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
