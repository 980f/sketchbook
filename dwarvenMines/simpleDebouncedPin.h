#pragma once
#include "simplePin.h"
#include "simpleTicker.h"
#include "cheaptricks.h"
struct DebouncedInput : public Printable {
  SimpleInputPin pin;
  //official state
  bool stable;
  //pending state
  bool bouncy;
  //filter timer
  Ticker bouncing;
  //usually the value is not changed, it is a program constant, we aren't forcing that as we might make it eeprom configurable.
  MilliTick DebounceDelay;

  //default for activeHigh is false as that is standard contact closure tech, pullup and switch to ground
  DebouncedInput(unsigned pinNumber, bool activeHigh = false, MilliTick DebounceDelay = ~0u): pin(pinNumber, activeHigh), DebounceDelay(DebounceDelay) {}

  /** @returns whether the input has officially changed to a new state */
  bool onTick(MilliTick ignored = 0) {
    //    Serial.printf("some input");
    if (changed(bouncy, bool(pin))) {//explicit cast need to get around a "const" issue with changed.
      bouncing.next(DebounceDelay);
      Serial.printf("Pin Changed: D%u to %x at %u, will report stable in %u\n", pin.number, bouncy, ignored, bouncing.remaining());
      return false;
    }

    if (bouncing.done()) {
      Serial.printf("Stable D%u:%x\n", pin, bouncy);
      return changed(stable, bouncy);
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
