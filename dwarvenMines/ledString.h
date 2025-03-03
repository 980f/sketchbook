#pragma once

//a complaint about template argument NUM_LEDS not found means that you did not declare: const unsigned NUM_LEDS = 89;
//#ifndef NUM_LEDS
//#error you must define NUM_LEDS to use this module
//#endif

#ifndef LEDStringType
#error you must define LEDStringType to use this module, format is for example: WS2811, 13, GRB
#error see FastLED.h in [yourSketchbook/]libraries/FastLED/src
#endif
//////////////////////////////////////////////////////////////////////
//if we don't define the following we get all sorts of class name clashes, there are plenty of generic named things in FastLED.
//there are failures to honor the namespace, no time to figur that out so we will rename outs, fuckit. #define FASTLED_NAMESPACE FastLed_ns
#define FASTLED_INTERNAL
#include <FastLED.h>

//using FastLed_ns::CRGB;
///////////////////////////////////////////////////////////////////////
template <unsigned NUM_LEDS> struct LedString {
  CRGB leds[NUM_LEDS];
  const CRGB ledOff = {0, 0, 0};

#define forLEDS(indexname) for (int indexname = NUM_LEDS; i-->0;)

  void all(CRGB same) {
    forLEDS(i) {
      leds[i] = same;
    }
  }

  void allOff() {
    all( ledOff);
  }

  using BoolPredicate = std::function<bool(unsigned)>;
  void setJust(CRGB runnerColor, BoolPredicate lit) {
    forLEDS(i) {
      leds[i] = lit(i) ? runnerColor : ledOff;
    }
  }

  void setup() {
    FastLED.addLeds<LEDStringType>(leds, NUM_LEDS);//FastLED tends to configuring the GPIO, most likely as a pwm/timer output.
  }

  void show() {
    FastLED.show();
  }

  CRGB & operator [](unsigned i) {
    i %= NUM_LEDS; //makes it easier to marquee
    return leds[i];
  }

  static CRGB blend(unsigned phase, unsigned cycle, const CRGB target, const CRGB from) {//todo: CRGB class has a blend method we can use here.
    return CRGB (
             map(phase, 0, cycle, target.r, from.r),
             map(phase, 0, cycle, target.g, from.g),
             map(phase, 0, cycle, target.b, from.b)
           );
  }

};
