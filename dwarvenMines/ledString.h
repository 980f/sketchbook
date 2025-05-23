#pragma once


#ifndef LEDStringType
#error you must define LEDStringType to use this module, format is for example: WS2811, 13, GRB
#error see FastLED.h in [yourSketchbook/]libraries/FastLED/src
#endif
//////////////////////////////////////////////////////////////////////
//if we don't define the following we get all sorts of class name clashes, there are plenty of generic named things in FastLED.
//BUT there are failures in FastLED to honor the namespace, no time to figure that out so we will rename ours, fuggit. #define FASTLED_NAMESPACE FastLed_ns
#define FASTLED_INTERNAL
#include <FastLED.h>

//using FastLed_ns::CRGB;
///////////////////////////////////////////////////////////////////////
//
struct LedStringer {
  static Print *spew;//diagnostics control
  static bool debugPattern;// = false;
  unsigned quantity;
  CRGB *leds;

  LedStringer (unsigned quantity, CRGB *leds): quantity(quantity), leds(leds) {
    if (spew && leds == nullptr) {
      spew->println("Null LEDS ARRAY!");
      quantity = 0;
    }
    allOff();
  }

  LedStringer (unsigned quantity): LedStringer(quantity, new CRGB(quantity + 1)) {} //+1 so that we can use pixel 0 if we are given a horrible address.

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
    if (spew) {
      spew->printf("setting all leds to %0X\n", same.as_uint32_t());
    }
    forLEDS(i) {
      leds[i] = same;
    }
  }

  void allOff() {
    all( Off );
  }

  struct Pattern {//being printable precludes {} init : Printable {
    //first one to set
    unsigned offset = 0;
    //set this many in a row,must be at least 1
    unsigned run = 0;
    //every this many, must be greater than or the same as run
    unsigned period = 0;
    //this number of times, must be at least 1
    unsigned sets = 0;
    //length of strand, to ensure we don't go off end of pixels
    unsigned max = 0;

    //making the class directly Printable loses us brace init, so we mimic that until we have the time to write some constructors
    size_t printTo(Print &dbg) const {
      return dbg.printf("Offset: %u\tRun: %u\tPeriod: %u\tSets: %u\tMax: %u\n", offset, run, period, sets, max);
    }

    /** @returns number of LEDS in the pattern, an idiot checker for setPattern() */
    unsigned expected() const {
      return run * sets;
    }

    /** functionality dropped, but may wish to restore so we keep this function as a no-op.
    */
    unsigned operator()(unsigned rawcomputation) const {
      return rawcomputation;
    }

    bool isViable() const {
      return sets != ~0 && sets > 0 && run != ~0 && run > 0 && period >= run && offset < max;
    }

    /** @returns whether this pattern is usable*/
    operator bool() const {
      return isViable();
    }

    void makeViable() {
      if (period < run) {
        period = run;
      }
    }

    struct Runner {
      const Pattern &pattern;
      //set this many in a row
      unsigned run;
      //this number of times
      unsigned set;
      unsigned latest;

      void restart() {
        if (debugPattern && spew) {
          spew->println("(Re)starting pattern");
        }
        latest = pattern.offset;
        set = pattern.sets;
        run = pattern.run;
      }

      /** computes next and @returns whether there actually is a valid one
          designed to work in the while() in a do{} while();
      */
      bool next() {
        if (!run || run == ~0) { // ~0 case for guard against ~ entered in debug UI.
          if (spew) {
            spew->println("Trivial run, ignoring it. \n");
          }
          return false; //we are done or have been done
        }
        if (--run) {
          ++latest;
          return latest < pattern.max;
        }
        if (debugPattern && spew) {
          spew->println("\nOne run completed. \t");
        }
        if (set != ~0 && --set > 0) {
          if (debugPattern && spew) {
            spew->printf("Remaining sets %u\n", set);
          }
          run = pattern.run;
          latest += pattern.period - pattern.run + 1; //run of 1 period of 1 skip 0? check;run of 1 period 2 skip 1?check;
          return true;
        }
        spew->println("Pattern completed.");
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
    if (quantity == 0) {
      if (spew) {
        spew->println("Quantity is zero, not even thinking of computing an address as we have no pixels to address");
      }
      return 0;//hopefully this is minimal harm.
    }
    unsigned numberSet = 0; //diagnostic
    if (pattern) {//test if it is valid, will produce at least one pixel address
      if (spew) {
        spew->print("Setting pattern \t");
        pattern.printTo(*spew);
        spew->printf("\tto color: %06X\n", color.as_uint32_t());
      }

      auto runner = pattern.runner();
      do {//precheck of pattern lets us know that at least one pixel is to be set
        unsigned pi = runner;
        leds[pi < quantity ? pi : 0] = color; //wrapping is better than altering unowned memory.
        ++numberSet;
        if (debugPattern && spew) {
          spew->printf(" %u\t", pi);
        }
      } while (runner.next());
    }
    if (spew) {
      spew->printf("\nSet %u pixels\n", numberSet);
    }
    return numberSet;//should == pattern.expected();
  }

  void setup() {
    if (spew) {
      spew->printf("FLED:addLeds <%s>(%p,%u)\n", " LEDStringType " , leds, quantity);
    }
    FastLED.addLeds<LEDStringType>(leds, quantity);//FastLED tends to configuring the GPIO, most likely as a pwm/timer output.
  }

  void setup(unsigned quantity, CRGB *leds = nullptr) {
    if (spew) {
      spew->printf("LedStringer setting up %d @ %p\n", quantity, leds);
    }
    this->quantity = quantity;
    this->leds = (!leds && quantity) ? new CRGB(quantity) : leds;
    setup();
  }

  void show() {
    auto elapsed = -micros();
    if (debugPattern && spew) {
      spew->printf("Calling FastLED.show() at %u\n", millis());
    }
    FastLED.show();
    elapsed += micros();
    if (spew) {
      spew->printf("FastLED.show() took %u uS\n", elapsed);
    }
  }

  CRGB & operator [](unsigned i) {
    i %= quantity; //makes it easier to marquee
    return leds[i];
  }

  //  static CRGB blend(unsigned phase, unsigned cycle, const CRGB target, const CRGB from) {//todo: CRGB class has a blend method we can use here.
  //    return CRGB (
  //             map(phase, 0, cycle, target.r, from.r),
  //             map(phase, 0, cycle, target.g, from.g),
  //             map(phase, 0, cycle, target.b, from.b)
  //           );
  //  }

};

Print *LedStringer::spew;//diagnostics control
bool LedStringer::debugPattern = false;

/** @deprecated untested
   statically allocate the array
*/
template <unsigned NUM_LEDS> struct LedString: public LedStringer {
  CRGB leds[NUM_LEDS];
  LedString(): LedStringer {NUM_LEDS, leds} {}
};

#if 0  //DOC block
/* Despite the #if 0 some tool still parsed the following block.
  The fast mode timing is 1.25 uS per bit, 24 bits per pixel, so 30 uSec per pixel.
  The first time you call FastLED.show() it takes about 1 millisecond longer than other calls, OR perhaps I was seeing a beat with my program's invocation.
  With 400 pixels I saw around 570 uS overhead per show, but the data doesn't start streaming to the pixels until that time is nearly up.
  That indicates to me that the first time show() is called some dynamic memory allocation is occuring, and that subsequently the time is how long it takes to expand the three bytes into 24 bits.

  For my 400 pixel example I should avoid calling show() for 12+ ms after the previous call if I don't want to get hit with a block.
*/
#endif
