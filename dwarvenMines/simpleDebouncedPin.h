#pragma once
#include "simplePin.h"
#include "simpleTicker.h"
#include "cheaptricks.h"

struct DebouncedInput : public Printable {
  bool trace = false;

  SimpleInputPin pin;
  //official state
  bool stable = false;
  //pending state
  bool bouncy = false;
  //filter timer
  Ticker bouncing;
  //usually the value is not changed, it is a program constant, we aren't forcing that as we might make it eeprom configurable.
  MilliTick DebounceDelay;

  //default for activeHigh is false as that is standard contact closure tech, pullup and switch to ground
  DebouncedInput(unsigned pinNumber, bool activeHigh = false, MilliTick DebounceDelay = ~0u): pin(pinNumber, activeHigh), DebounceDelay(DebounceDelay) {}

  /** @returns whether the input has officially changed to a new state */
  bool onTick(MilliTick now) {
    if (changed(bouncy, bool(pin))) {//explicit cast need to get around a "const" issue with changed.
      bouncing.next(DebounceDelay);
      if (TRACE && trace) {
        Serial.printf("Pin Changed: D%u to %x at %u, will report stable in %u\n", pin.number, bouncy, now, bouncing.remaining());
      }
      return false;
    }

    if (bouncing.done() && changed(stable, bouncy)) {
      if (TRACE && trace) {
        Serial.printf("Stable D%u:%x at %u\n", pin.number, stable, now);
      }
      return true;
    }
    return false;
  }

  //use ~pin (not -pin) to indicate low active sense
  void setup(bool triggerOnStart = false) {
    pin.setup();
    //    pin << true;//JIC
    bouncy = pin;

    if (triggerOnStart) {
      stable = ~bouncy;
      bouncing.next(DebounceDelay);
    } else {
      stable = bouncy;
    }
  }

  void filter(MilliTick filter) {
    DebounceDelay = filter;
  }

  operator bool() {
    return stable;
  }

  bool isStable() {
    return !bouncing.isRunning();
  }

  size_t printTo(Print& p) const override {
    return p.print(stable ? "ON" : "off") + p.print(bouncy != stable ? "~" : "." );
  }
};
