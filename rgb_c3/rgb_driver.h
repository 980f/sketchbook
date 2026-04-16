#pragma once
// todo: see if we have a more complete class somewhere, such as in FastLED.
struct Color {
  unsigned r;
  unsigned g;
  unsigned b;

  void apply(char color, unsigned rawvalue) {
    switch (color) {
      case 'r':
        r = rawvalue;
        break;
      case 'g':
        g = rawvalue;
        break;
      case 'b':
        b = rawvalue;
        break;
    }
  }

  
};

/** access to the hardware */
#include "ESP32Servo.h"
struct RGB_C3 {
  // todo: pick 3 pins
  Color desired; // retain applied value for debug, and as a convenience for manipulation

  void apply(Color &desired) {
    // todo: send 'desired' values to hardware.
    this->desired = desired;
  }

  void apply(char color, unsigned rawvalue) {
    desired.apply(color,rawvalue);
  }

  void refresh(){
    apply(desired);
  }

};
