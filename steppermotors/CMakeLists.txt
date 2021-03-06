cmake_minimum_required(VERSION 2.8.4)
unset(QT_QMAKE_EXECUTABLE)
set(CMAKE_CXX_STANDARD 14)


set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/ArduinoToolchain.cmake)
set(PROJECT_NAME steppermotors)
project(${PROJECT_NAME})

set(${CMAKE_PROJECT_NAME}_SKETCH steppermotors.ino)

# Uncomment below additional settings as needed.
set(${CMAKE_PROJECT_NAME}_BOARD leonardo)
set(${CMAKE_PROJECT_NAME}_PORT /dev/ttyACM0)
# set(mega.build.mcu atmega2560)
# set(mega.upload.protocol wiring)
# set(mega.upload.speed 115200)

set(ARDUINO_SDK_PATH /d/bin/arduino-1.8.9)

set(ARDLIBS Wire)
#sdk path not found in a timely fashion despite the above declaration
#include_directories(${ARDUINO_SDK_PATH}/hardware/arduino/avr/libraries/Wire/src)
include_directories(${ARDUINO_SDK_PATH}/hardware/arduino/avr/cores/arduino)

#arduino IDE effectively does this:
FILE(GLOB ${CMAKE_PROJECT_NAME}_SRCS *.cpp)
#we will be selective:  but didn't get a list in the project window so seeing if glob will do that
#set(${CMAKE_PROJECT_NAME}_SRCS char.cpp cstr.cpp  millievent.cpp microevent.cpp sprinter.cpp textkey.cpp spibridges.cpp )
#but include all headers, that is cheap and helps the IDE
FILE(GLOB ${CMAKE_PROJECT_NAME}_HDRS *.h)

generate_arduino_firmware(${PROJECT_NAME})
