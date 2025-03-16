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
//
struct LedStringer {
  static Print *spew;//diagnostics control
  unsigned quantity;
  CRGB *leds;

  LedStringer (unsigned quantity, CRGB *leds): quantity(quantity), leds(leds) {}

  LedStringer (unsigned quantity): quantity(quantity), leds(new CRGB(quantity)) {}

  LedStringer (): LedStringer(0, nullptr) {}

  static constexpr CRGB Off = {0, 0, 0};

#define forLEDS(indexname) for (int indexname = quantity; i-->0;)

  using BoolPredicate = std::function<bool(unsigned)>;
  /** this is poorly named as it sets all leds to something, either the @param runnerColor or black */
  void all(CRGB runnerColor, BoolPredicate lit) {
    forLEDS(i) {
      leds[i] = lit(i) ? runnerColor : Off;
    }
  }

  /** sets all pixels to @param same color.
      This could be implemented by passing an "always true" predicate to all(two args), but this way is faster and simpler.
  */
  void all(CRGB same) {
    forLEDS(i) {
      leds[i] = same;
    }
  }

  void allOff() {
    all( Off );
  }

  struct Pattern /*: Printable*/ {
    //first one to set, note that modulus does get applied to this.
    unsigned offset;
    //set this many in a row,must be at least 1
    unsigned run;
    //every this many, must be greater than or the same as run
    unsigned period;
    //this number of times, must be at least 1
    unsigned sets;
    //Runner will apply this modulus to its generated numbers
    unsigned modulus;

    //making the class directly Printable loses us brace init, so we mimic that until we have the time to write some constructors
    size_t printTo(Print &dbg) const {
      return dbg.printf("Offset: %u\tRun: %u\tPeriod: %u\tSets: %u\tMod: %u\n", offset, run, period, sets, modulus);
    }

    /** @returns number of LEDS in the pattern, an idiot checker for setPattern() */
    unsigned expected() const {
      return run * sets;
    }

    /** we want to wrap the value used as an array index, without altering our logical counter */
    unsigned operator()(unsigned rawcomputation) const {
      return modulus ? rawcomputation % modulus : rawcomputation;
    }

    /** @returns whether this pattern is usable*/
    operator bool() const {
      return sets > 0 && run > 0 && period >= run;
    }

    struct Runner {
      const Pattern &pattern;
      //set this many in a row
      unsigned run;
      //this number of times
      unsigned set;
      unsigned latest;

      void restart() {
        latest = pattern.offset;
        set = pattern.sets;
        run = pattern.run;
      }

      //computes next and @returns whether there actually is a valid one
      bool next() {
        if (run == ~0) { //we are done
          return false; //have been done
        }
        if (run-- > 0) {
          ++latest;
          return true;
        }
        if (set-- > 0) {
          run = pattern.run;
          latest += pattern.period - pattern.run; //run of 1 period of 1 skip 0? check;run of 1 period 2 skip 1?check;
          return true;
        }
        //latest stays at final valid value.
        return false;//just became done.
      }

      //@returns the value computed by next,
      operator unsigned () const {
        return pattern(latest);
      }

      Runner(const Pattern &pattern): pattern(pattern) {
        restart();
      }
    };

    Runner runner() const {
      return Runner(*this);
    }

    //some factories
    /** example: period 5, range 50 will set 10 pixels spaced 5 apart starting with start */
    static Pattern EveryNth(unsigned period, unsigned range, unsigned start, unsigned wrap = 0) {
      return Pattern {start, 1, period, range / period, wrap ? wrap : range};
    }

  };


  /** set the pixels defined by @param pattern to @param color, other pixels are not modified */
  unsigned setPattern(CRGB color, const Pattern &pattern) {
    unsigned numberSet = 0; //diagnostic
    if (pattern) {
      if (spew) {
        spew->print("Setting pattern \t");
//        spew->print(pattern);
        pattern.printTo(*spew);
        spew->printf("\tcolor: %06X\n", color.as_uint32_t());
      }

      auto runner = pattern.runner();
      do {//precheck of pattern lets us know that at least one pixel gets set
        unsigned pi = ~0u;
        leds[pi = runner] = color;
        ++numberSet;
        if (spew) {
          spew->printf(" %u\t", pi);
        }
      } while (runner.next());
    }
    if (spew) {
      spew->printf("\n\tSet %u pixels\n", numberSet);
    }
    return numberSet;//should == pattern.expected();
  }

  void setup() {
    FastLED.addLeds<LEDStringType>(leds, quantity);//FastLED tends to configuring the GPIO, most likely as a pwm/timer output.
  }

  void setup(unsigned quantity, CRGB *leds = nullptr) {
    if (spew) {
      spew->printf("LedStringer setting up %d @ %p\n", quantity, leds);
    }
    this->quantity = quantity;
    this->leds = (!leds && quantity) ? new CRGB(quantity) : leds;
  }

  void show() {
    FastLED.show();
  }

  CRGB & operator [](unsigned i) {
    i %= quantity; //makes it easier to marquee
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

Print *LedStringer::spew = nullptr;

//statically allocate the array
template <unsigned NUM_LEDS> struct LedString: public LedStringer {
  CRGB leds[NUM_LEDS];
  LedString(): LedStringer {NUM_LEDS, leds} {}
};
