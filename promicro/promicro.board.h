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
    const OutputPin<24> led0;//24 or 30 , documents differ.


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
        };

        T1Control() {}

        static const unsigned divisors[ByRising + 1] = {0, 1, 8, 64, 256, 1024, 1, 1};

        static unsigned resolution(CS cs) {
          return divisors[cs & 7];
        }

        /** ticks must be at least 3, typically as large as you can make it for a given CS.
            example: 50Hz@16MHz: 320k ticks, use divide By8 and 40000.
        */
        void setPwmBase(unsigned ticks, CS cs)const { //
          TCCR1A = 0;// set entire TCCR1A register to 0
          TCCR1B = 0;// same for TCCR1B
          TCNT1  = 0;//initialize counter value to 0
          // set compare match register
          ICR1 = ticks;
          // turn on CTC mode with ICR as period definer (WGM1.3:0 = 12
          //          TCCR1A is   oc<<2*num, num=1..3, value: 1x simple pwm, x=polarity, 01: toggle, 00: not in use
          TCCR1B = (1 << WGM12) | cs;//3 lsbs are clock select. 0== fastest rate. Only use prescalars when needed to extend range.
          // enable timer compare interrupt
          TIMSK1 |= (1 << OCIE1A);
        }

        template <unsigned which> class PwmBit {
            enum {shift = which * 2, mask = 3 << shift};
            void configure(unsigned twobits) const {
              mergeInto(TCCR1A, (twobits << shift), mask); //todo: bring out the bit field class.
            }

          public:
            void toggle()const {
              configure(1);
            }

            void off() {
              configure(0);
            }


            void setDuty(unsigned ticks, bool invertit = 0)const {
              if (ticks) { //at least 1 enforced here
                configure(2 + invertit);
                (&TCNT1)[which] = ticks - 1;
              } else {     //turn it off.
                off();
              }
            }

            unsigned clip(unsigned &ticks) const {
              if (ticks > ICR1) {
                ticks = ICR1;
              }
              return ticks;
            }

        };

        /** @returns the prescale setting required for the number of ticks. Will be Halted if out of range.*/
        static constexpr CS prescaleRequired(long ticks) {
          //          if (ticks >= (1 << (16 + 10))) {
          //            return Halted;//hopeless
          //          }
          //          if (ticks >= (1 << (16 + 8))) {
          //            return By1K;
          //          }
          //          if (ticks >= (1 << (16 + 6))) {
          //            return By256;
          //          }
          //          if (ticks >= (1 << (16 + 3))) {
          //            return By64;
          //          }
          //          if (ticks >= (1 << (16 + 0))) {
          //            return By8;
          //          }
          //          return By1;
          //older compiler makes me pack the above into nested ternaries. the thing added to 16 is the power of two that the next line will return
          return  (ticks >= (1 << (16 + 10))) ? Halted : //out of range
                  (ticks >= (1 << (16 + 8))) ? By1K :
                  (ticks >= (1 << (16 + 6))) ? By256 :
                  (ticks >= (1 << (16 + 3))) ? By64 :
                  (ticks >= (1 << (16 + 0))) ? By8 :
                  By1;
        }

        unsigned  clip(unsigned &ticks) const {
          if (ticks > 65535) {
            ticks = 65535;
          }
          return ticks;
        }

    } T1;
};
