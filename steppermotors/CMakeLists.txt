cmake_minimum_required(VERSION 2.8.4)

set(CMAKE_CXX_STANDARD 14)


set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/ArduinoToolchain.cmake)
set(PROJECT_NAME steppermotors)
project(${PROJECT_NAME})

set(${CMAKE_PROJECT_NAME}_SKETCH steppermotors.ino)

#### Uncomment below additional settings as needed.
 set(${CMAKE_PROJECT_NAME}_BOARD leonardo)
 set(${CMAKE_PROJECT_NAME}_PORT /dev/ttyACM0)
# set(mega.build.mcu atmega2560)
# set(mega.upload.protocol wiring)
# set(mega.upload.speed 115200)

set(ARDUINO_SDK_PATH /d/bin/arduino-1.8.9)

set(ARDLIBS Wire)
#sdk path not found in a timely fashion
include_directories(${ARDUINO_SDK_PATH}/hardware/arduino/avr/libraries/Wire/src)


#arduino does this: FILE(GLOB ${PROJECT_NAME}_SRCS *.cpp)
#we will be selective:
set(${CMAKE_PROJECT_NAME}_SRCS char.cpp clockhand.cpp cstr.cpp hms.cpp millievent.cpp microevent.cpp sprinter.cpp textkey.cpp )
#but include all headers, that is cheap and helps the IDE
FILE(GLOB ${CMAKE_PROJECT_NAME}_HDRS *.h)

generate_arduino_firmware(${PROJECT_NAME})
