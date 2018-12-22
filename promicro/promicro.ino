
//each variant of stm32 support has different defined symbols for the processor selection, trivially different.

#ifdef ARDUINO_ARCH_STM32F1
#define STMDUINO
#endif

#include "bitbanger.h"

#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"

#ifdef AVR
namespace T1Control {

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

const unsigned divisors[] = {0, 1, 8, 64, 256, 1024, 1, 1};

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

};
#endif

//const OutputPin<7, LOW> relay1;
////low turns relay on
//const OutputPin<9, LOW> relay2;

const OutputPin<LED_BUILTIN> BugLight;

const OutputPin < 16 + 9 > ph0;
const OutputPin < 16 + 8 > ph1;
const OutputPin < 16 + 7 > ph2;
const OutputPin < 16 + 6 > ph3;

const InputPin < 16 + 12 > fasterButton;
const InputPin < 16 + 15 > slowerButton;

const InputPin < 16 + 13 > thisButton;
const InputPin < 16 + 14 > thatButton;

static const unsigned grey2[] = {0, 1, 3, 2};
class Stepper {

  public:
    enum Iface {Uni4 = 0, Bip4 = 1}; //we are slow enough to not want to pay extra for virtual function call.
    Iface iface = Bip4; //
    int step = 0;
    //  unsigned perRevolution=200;
    //  unsigned phase=0;
    //

    void applyPhase(unsigned phase) {
      switch (iface) {
        case Uni4: {
            unsigned bits = 0x33 >> (phase % 4);
            ph0 = bitFrom(bits, 0);
            ph1 = bitFrom(bits, 1);
            ph2 = bitFrom(bits, 2);
            ph3 = bitFrom(bits, 3);
          }
          break;
        case Bip4: {
            unsigned bits = grey2[phase & 3]; //Gray code count,
            ph0 = bitFrom(bits, 0);
            ph1 = bitFrom(~bits, 0);
            ph2 = bitFrom(bits, 1);
            ph3 = bitFrom(~bits, 1);
          } break;
      }
    }

    void operator ()(bool fwd) {
      step += fwd ? 1 : -1;
      applyPhase(step);
    }

    void operator ++() {
      ++step;
      applyPhase(step);
      //    if((++phase)==perRevolution){
      //      phase=0;
      //    }

    }

    void operator --() {
      --step;
      applyPhase(step);
      //    if((phase==0){
      //      phase=perRevolution;
      //    }
      //  --phase;

    }
};

#ifdef TXLED1
class ProMicro {
  public:
    struct TxLed {
      bool lastset;
      operator bool()const {
        return lastset;
      }
      bool operator=(bool setting) {
        lastset = setting;
        lastset ? TXLED1 : TXLED0; //vendor macros, should replace with an OutputPin
        return setting;
      }
    };
};

#endif



//one must peruse the compiler output to find what flags can be used to conditionalize per target.
#ifdef STMDUINO
class TimeBase {
  public:
    void setDivider(unsigned perStep) {

    }

    void setTicks(unsigned perStep) {
      Timer3.setPeriod(perStep);
    }

    void setIsr(voidFuncPtr routine) {
      Timer3.setChannel1Mode(TIMER_OUTPUT_COMPARE);
      Timer3.setCompare1(1);

      Timer3.attachCompare1Interrupt(routine);

      Timer3.refresh();
      Timer3.resume();

    }

};
//set up a tick interrupt with a interval of 1 ms.

#endif

unsigned thespeed = 60000;
unsigned speedstep = 100;

Stepper positioner;

TimeBase ticker;

bool clockwise = false;
bool beRunning = false;

unsigned blips = 0;

void theisr()
{ //timer1 interrupt at step rate
  if (beRunning) {
    ++blips;
    positioner(clockwise);
  }
}

void setup() {
  ticker.setIsr(theisr);
  ticker.setTicks(thespeed);
  Serial.begin(500000);//number doesn't matter.
}

void upspeed(unsigned newspeed) {
  BugLight.toggle();
  beRunning = true;
  if (changed(thespeed, newspeed)) {

    //    T1Control::clip(thespeed);
    //    T1Control::setDivider(thespeed);
  }
}

void loop() {
  if (MilliTicked) { //this is true once per millisecond.
    if (fasterButton) {
      upspeed(thespeed + speedstep);
    }
    if (slowerButton) {
      upspeed(thespeed - speedstep);
    }
  }

  if (Serial) {
    if (Serial.available()) {
      auto key = Serial.read();
      Serial.print(char(key));//echo.
      switch (key) {
        case ' ':
          Serial.print("Speed:");
          Serial.println(thespeed);

          Serial.print("Location:");
          Serial.println(positioner.step);


          Serial.print("Blips:");
          Serial.println(blips);

          break;
        case '1': case '2': case '3': case '4': {//jump to phase
            positioner.applyPhase(key - 1);
          }
          break;
        case 'q':
          positioner.step &= 3; //closest to 0 we can get while the phases are still tied to position.
          break;
        case 'w':
          ++positioner;
          Serial.println(positioner.step);
          break;
        case 'e':
          --positioner;
          Serial.println(positioner.step);
          break;
        case 'r':
          clockwise = false;
          break;
        case 'f':
          clockwise = true;
          break;
        case 'u':
          speedstep -= 10;
          break;
        case 'j':
          speedstep += 10;
          break;
        case 'y':
          upspeed(thespeed + speedstep);
          break;
        case 'h':
          upspeed(thespeed - speedstep);
          break;
        default:
          Serial.print("?\n");
          break;
        case '\n':
        case '\r':
          //ignore end of line used to flush letter commands.
          break;
      }
    }
  }
}
