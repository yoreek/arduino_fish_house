set(CMAKE_TOOLCHAIN_FILE /home/yura/robot/lib/arduino-cmake-master/cmake/ArduinoToolchain.cmake)
set(ARDUINO_SDK_PATH /home/yura/robot/arduino)
link_directories(/home/yura/sketchbook/libraries)

set(${FIRMWARE_NAME}_PORT /dev/ttyACM0)
set(ARDUINO_DEFAULT_BOARD uno) # Default Board ID, when not specified
set(ARDUINO_DEFAULT_PORT /dev/ttyACM0) # Default Port, when not specified

cmake_minimum_required(VERSION 2.8)

#====================================================================#
#  Setup Project                                                     #
#====================================================================#
project(fish_house C CXX)

set(${PROJECT_NAME}_MAJOR_VERSION 0)
set(${PROJECT_NAME}_MINOR_VERSION 1)
set(${PROJECT_NAME}_PATCH_VERSION 0)
set(${PROJECT_NAME}_VERSION
    ${${PROJECT_NAME}_MAJOR_VERSION}.${${PROJECT_NAME}_MINOR_VERSION}.${${PROJECT_NAME}_PATCH_VERSION})

configure_file (
    "${PROJECT_SOURCE_DIR}/Config.h.in"
    "${PROJECT_SOURCE_DIR}/Config.h"
)
print_board_list()
print_programmer_list()

set(fish_house_BOARD uno)
set(fish_house_SKETCH .)
set(fish_house_PORT /dev/ttyACM0)
set(fish_house_SERIAL picocom @SERIAL_PORT@ -b 9600 -l)

generate_arduino_firmware(fish_house)
