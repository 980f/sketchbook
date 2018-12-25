

#include "bitbanger.h"

#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"
#include "minimath.h"

#define dbg Serial1

#include "promicro.board.h"
ProMicro board;

//0,1 are rx,tx used by Serial1
const OutputPin<2, LOW> T2;
const OutputPin<3> T3;
const OutputPin<4> T4;
const OutputPin<5> T5;
const OutputPin<6, LOW> T6;
const OutputPin<7, LOW> T7;
const OutputPin<8, LOW> T8;
const OutputPin<9, LOW> T9;
const OutputPin<10, LOW> T10;
//we are doing these in geographic order, which after 10 is non-sequential
const OutputPin<16, LOW> T16;
const InputPin<14> fasterButton;
const InputPin<15> slowerButton;
//A0,A1,A2,A3


/**normalize analog operations to full range of processor natural data type, number of bits is a local detail.*/

template <unsigned numBits = 8> struct AnalogValue {
  unsigned allbits;//all ones for maximum value, all zeroes for minimum.
  enum {
    shift = ProMicro::unsigned_bits - numBits, //number of bits in abstract type versus number in hardware controller
    quantum = 1 << shift,
  };

  AnalogValue(unsigned physical) {
    allbits = physical << shift;
  }

  unsigned operator =(unsigned physical) {
    allbits = physical << shift;
    return allbits;
  }

  unsigned operator =(int physical) {
    if (physical >= 0) {
      allbits = physical << shift;
      return allbits;
    } else {
      return 0;
    }
  }


  operator unsigned()const {
    return allbits;
  }

  unsigned actual()const {
    return allbits >> shift;
  }

};

template <unsigned numBits> struct AnalogOutput {
  const unsigned pinNumber;
  AnalogOutput(unsigned pinNumber): pinNumber(pinNumber) {
    //one day will check numBits and up the resolution for boards which allow that.
  }

  //all ones for max, 0 for min.
  void operator =(AnalogValue<numBits> allbits) const {
    analogWrite(pinNumber, allbits.actual());
  }
};

template <unsigned numBits> struct AnalogInput {
  const unsigned pinNumber;
  AnalogInput(unsigned pinNumber): pinNumber(pinNumber) {

  }

  //all ones for max, 0 for min.
  operator AnalogValue<numBits>() const {
    T9 = 1;
    unsigned actual = 42;
    //analogRead(pinNumber);
    T9 = 0;
    T8 = 1;
    AnalogValue<numBits> allbits =   actual ;
    T8 = 0;
    return allbits;
  }
};

AnalogOutput<8> Xdrive(9);
AnalogOutput<8> Ydrive(10);

AnalogInput<8> joyX(A3);
AnalogInput<8> joyY(A2);

AnalogValue<8> rawX(0);
AnalogValue<8> rawY(0);
void showJoy() {
  dbg.print("Joy x-y:");
  dbg.print(rawX);
  dbg.print(",");
  dbg.println(rawY);
}

#include "steppertest.h"

void setup() {
  Serial.begin(500000);//number doesn't matter.
  Serial1.begin(115200);//hardware serial. up the baud to reduce overhead.
  stepperSetup();
  dbg.println("Howdy");
  T4 = 1;
  T5=1;
  T6=1;
  
}


void loop() {
  static unsigned iters = 0;
  ++iters;
  if (MilliTicked) { //this is true once per millisecond.
    //    dbg.println(MilliTicked.recent());
    T2.flip();
    if (fasterButton) {
      upspeed(thespeed + speedstep);
    }
    if (slowerButton) {
      upspeed(thespeed - speedstep);
    }
    if (MilliTicked.every(93)) {
      T7 = 1;
      rawX = joyX;
      T7 = 0;
      //    rawY = joyY;

      Xdrive = rawX;
      Ydrive = rawY;
    }

    if (MilliTicked.every(1000)) {
      dbg.println(" milli");
      T3.flip();
      showJoy();
    }
  }


  if (dbg && dbg.available()) {
    T4.flip();
    auto key = dbg.read();
    dbg.print(char(key));//echo.
    dbg.print(':');//echo.

    switch (key) {
      case 'p':
        showJoy();
        break;

      case 'i': board.led0 = 0; break;
      case 'k': board.led0 = 1; break;
      case 'o': board.led1 = 0; break;
      case 'l': board.led1 = 1; break;

      default:
        if (!stepCLI(key)) {
          dbg.print("?\n");
        }
        break;
      case '\n'://clear display via shoving some crlf's at it.
      case '\r':
        dbg.println("\r\n");
        break;
    }
  }

}
