#pragma once
#include "simplePin.h"
#include "simpleTicker.h"
#include "cheaptricks.h"
struct DebouncedInput : public Printable {
  SimplePin pin;
  //official state
  bool stable;
  //pending state
  bool bouncy;
  //filter timer
  Ticker bouncing;
  //usually the value is not changed, it is a program constant, we aren't forcing that as we might make it eeprom configurable.
  MilliTick DebounceDelay;

  DebouncedInput(unsigned pinNumber, bool activeHigh = false, MilliTick DebounceDelay = ~0u): pin(pinNumber, activeHigh), DebounceDelay(DebounceDelay) {}

  /** @returns whether the input has officially changed to a new state */
  bool onTick(MilliTick ignored = 0) {
    //    Serial.printf("some input");
    if (changed(bouncy, bool(pin))) {//explicit cast need to get around a "const" issue with changed.
      Serial.printf("Pin Changed: D%u to %x\n", pin.number, bouncy);
      bouncing.next(DebounceDelay);
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
    pin.setup(INPUT);
    pin = true;//JIC
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
    return p.print(stable ? "ON" : "off");
  }
};
