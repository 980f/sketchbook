//board model, includes chip type.
//from leonardo variant/pins....h
//#define LED_BUILTIN 13
//#define LED_BUILTIN_RX 17
//#define LED_BUILTIN_TX 30
//a0<->18  a1<->19 ... for 12 a ins.
//  TIMER1A,    /* 9 */
//  TIMER1B,    /* 10 */
class ProMicro {
  public:
    enum {//processor constants
      unsigned_bits = 16, //AVR does not provide much of C++ library :(

    };
    //
    //    struct TxLed {
    //      bool lastset;
    //      operator bool()const {
    //        return lastset;
    //      }
    //      bool operator=(bool setting) {
    //        lastset = setting;
    //        lastset ? TXLED1 : TXLED0; //vendor macros, should replace with an OutputPin
    //        return setting;
    //      }
    //    } txled;
    //
    //todo: get digitalpin class to function so that we can pass pin numbers.
    const OutputPin<17> led1;
    const OutputPin<24> led0;


    class T1Control {
      public:
        enum  CS {
          Halted = 0,
          By1 = 1,
          By8 = 2,
          By64 = 3,
          By256 = 4,
          By1K = 5,
          ByFalling = 6,
          ByRising = 7
        } cs;

        T1Control(): cs(Halted) {}

        const unsigned divisors[ByRising + 1] = {0, 1, 8, 64, 256, 1024, 1, 1};

        unsigned resolution() {
          return divisors[cs & 7];
        }

        /** set cs first */
        void setDivider(unsigned ticks) { //
          TCCR1A = 0;// set entire TCCR1A register to 0
          TCCR1B = 0;// same for TCCR1B
          TCNT1  = 0;//initialize counter value to 0
          // set compare match register
          OCR1A = ticks;
          // turn on CTC mode
          TCCR1B = (1 << WGM12) | cs;//3 lsbs are clock select. 0== fastest rate. Only use prescalars when needed to extend range.
          // enable timer compare interrupt
          TIMSK1 |= (1 << OCIE1A);
        }

        /** @returns the prescale setting required for the number of ticks. Will be Halted if out of range.*/
        CS prescaleRequired(long ticks) {
          if (ticks >= (1 << (16 + 10))) {
            return Halted;//hopeless
          }
          if (ticks >= (1 << (16 + 8))) {
            return By1K;
          }
          if (ticks >= (1 << (16 + 6))) {
            return By256;
          }
          if (ticks >= (1 << (16 + 3))) {
            return By64;
          }
          if (ticks >= (1 << (16 + 0))) {
            return By8;
          }
          return By1;
        }

        unsigned  clip(unsigned &ticks) {
          if (ticks > 65535) {
            ticks = 65535;
          }
          return ticks;
        }

    } T1;
};
