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
            Note that we are this time ignoring the possibility of dicking with the shared prescalar.
        */
        void setPwmBase(unsigned ticks, CS cs)const { //pwm cycle time
          TCCR1A = 0;//disable present settings to prevent evanescent weirdness
          TCCR1B = 0;// ditto
          TCNT1  = 0;//to make startup repeatable
          ICR1 = ticks;
          // turn on CTC mode with ICR as period definer (WGM1.3:0 = b1110)
          TCCR1A = 2;//WGM1:0
          TCCR1B = (3 << 3) | cs; //3<<3 is WGM3:2. 3 lsbs are divisor select.
        }

        template <unsigned which> class PwmBit {
            enum {
              shift = which * 2,
              mask = 3 << shift,//two control bits for each
              Dnum = 8 + which, //Arduino digital label for output pi, used by someone somewhen to make pin be an output.
            };
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
//  address order: TCCR, ICR, OCRA, OCRB, OCRC
                (&ICR1)[which] = ticks - 1;
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

        PwmBit<1> pwmA;//PB5, D9
        PwmBit<2> pwmB;//PB6, D10
        // not available off chip       PwmBit<3> pwmC;
        void showstate(ChainPrinter &dbg) {
          dbg("\nT1\tCRA: ", TCCR1A, "\tCRB:", TCCR1B, "\tOA:", OCR1A, "\tOB:", OCR1B, "\tICR:", ICR1);
        }


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
