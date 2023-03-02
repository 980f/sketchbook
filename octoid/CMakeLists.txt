CMAKE_MINIMUM_REQUIRED(VERSION 3.21)
PROJECT(octoid)

SET(CMAKE_CXX_STANDARD 14)

INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(octoid
  chainprinter.h
  cheaptricks.h
  digitalpin.h
  edgyinput.h
  eztypes.h
  millievent.cpp
  millievent.h
  minimath.h
  octoid.h
  octoid.ino
  scani2c.h
  twiddler.cpp
  twiddler.h
  valuestacker.h)
