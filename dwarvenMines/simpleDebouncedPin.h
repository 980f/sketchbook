#pragma once
#include "simplePin.h"
#include "simpleTicker.h"
#include "simpleUtil.h"
struct DebouncedInput {
  SimplePin pin;
  //official state
  bool stable;
  //pending state
  bool bouncy;
  //filter timer
  Ticker bouncing;
  //usually the value is not changed, it is a program constant, we aren't forcing that as we might make it eeprom configurable.
  MilliTick DebounceDelay;

  DebouncedInput(const SimplePin &pin, MilliTick DebounceDelay = ~0u): pin(pin), DebounceDelay(DebounceDelay) {}

  /** @returns whether the input has officially changed to a new state */
  bool onTick(MilliTick ignored=0) {
    if (changed(bouncy, pin)) {
      bouncing.next(DebounceDelay);
    }

    if (bouncing.done()) {
      return changed(stable, bouncy);
    }
    return false;
  }

  //use ~pin (not -pin) to indicate low active sense
  void setup(bool triggerOnStart = false) {
    pin.setup(INPUT);
    bouncy = pin;

    if (triggerOnStart) {
      stable = ~bouncy;
      bouncing.next(DebounceDelay);
    } else {
      stable = bouncy;
    }
  }

  //mostly for when you can't manage to provide these arguments on construction:
  void attach(const SimplePin &simplepin, MilliTick filter) {
    pin=simplepin;
    DebounceDelay=filter;
  }

  operator bool() const {
    return stable;
  }

  bool isStable() const {
    return !bouncing.isRunning();
  }
};
