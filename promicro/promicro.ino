

#include "bitbanger.h"

#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"
#include "minimath.h"

#include "chainprinter.h"

ChainPrinter dbg(Serial1);

#define Console Serial1

#include "promicro.board.h"
ProMicro board;


//0,1 are rx,tx used by Serial1
//the test leds are low active.
const OutputPin<2, LOW> T2;
const OutputPin<3, LOW> T3;
const OutputPin<4, LOW> T4;
const OutputPin<5, LOW> T5;
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


/** will be adding smoothing to analog inputs so wrap the data now. */

struct AnalogValue {
  unsigned raw;

  AnalogValue() {//because pre-setup code can't talk to serial.
    raw = 0;
  }

  AnalogValue(int physical) {
    dbg("A!");
    raw = physical << 7; //todo: shift will be a function of input resolution (10 vs 12) and oversampling rate (8 samples is same as 3 bit shift)
  }

  unsigned operator =(int physical) {
    dbg("A=");
    raw = physical << 7;
    return raw;
  }

  operator unsigned()const {
    dbg("AU.");
    return raw;
  }

};

struct AnalogOutput {
    const unsigned pinNumber;
    AnalogOutput(unsigned pinNumber): pinNumber(pinNumber) {
      //one day will check numBits and up the resolution for boards which allow that.
    }

    //all ones for max, 0 for min.
    void operator =(AnalogValue av) const {
      analogWrite(pinNumber, av >> 7); //15 bit averaged input, cut it down to 8 msbs of those 15. todo: configure for 10 and maybe 16 bit output.
    }

    //all ones for max, 0 for min.
    void operator =(int raw) const {
      analogWrite(pinNumber, raw);
    }

  private:
    void operator =(AnalogOutput &other) = delete; //to stifle compiler saying that it did this on it own, good for it :P
};

template <unsigned numBits> struct AnalogInput {
  const unsigned pinNumber;
  AnalogInput(unsigned pinNumber): pinNumber(pinNumber) {

  }

  //all ones for max, 0 for min.
  operator AnalogValue/*<numBits>*/() const {
    dbg("AI.");
    unsigned actual = analogRead(pinNumber);
    T9 = 0;
    T8 = 1;
    AnalogValue/*<numBits>*/ allbits =   actual ;
    dbg("A. ");
    T8 = 0;
    return allbits;
  }
};

AnalogOutput Xdrive(9);
AnalogOutput Ydrive(10);

AnalogInput<8> joyX(A3);
AnalogInput<8> joyY(A2);

//AnalogValue
int rawX;
//AnalogValue
int rawY;

void showJoy() {
  dbg("\nJoy x:", rawX, " y:", rawY);
}

#include "steppertest.h"

void setup() {
  Serial.begin(500000);//number here doesn't matter.
  Serial1.begin(500000);//hardware serial. up the baud to reduce overhead.
  //  stepperSetup();
  dbg("\nHowdy");
  T4 = 1;
  T5 = 1;
  T6 = 1;

}

bool readanalog = false;
void loop() {
  static unsigned iters = 0;
  ++iters;
  if (MilliTicked) { //this is true once per millisecond.
    //    dbg.println(MilliTicked.recent());
    T2.flip();
    if (fasterButton) {
      //      upspeed(thespeed + speedstep);
      readanalog = false;
    }
    if (slowerButton) {
      //      upspeed(thespeed - speedstep);
      readanalog = true;
    }
    if (readanalog && MilliTicked.every(100)) {
      T7 = 1;
      rawX =  analogRead(A3);//joyX;
      T7 = 0;
      rawY = analogRead(A2);//joyY;
      showJoy();
      Xdrive = rawX;
      Ydrive = rawY;
    }

    if (MilliTicked.every(1000)) {
      //      dbg.println(" milli");
      T3.flip();
    }
  }


  if (Console&& Console.available()) {
    T4.flip();
    auto key = Console.read();
    dbg(char(key),':');//echo.

    switch (key) {
      case 'p':
        showJoy();
        break;

      case 'i':
        board.led0 = 0;
        T5 = 0;
        break;
      case 'k':
        board.led0 = 1;
        T5 = 1;
        break;
      case 'o':
        board.led1 = 0;
        T5 = 0;
        break;
      case 'l':
        board.led1 = 1;
        T5 = 1;
        break;

      default:
        if (!stepCLI(key)) {
          dbg("?\n");
        }
        break;
      case '\n'://clear display via shoving some crlf's at it.
      case '\r':
        dbg("\r\n");
        break;
    }
  }

}
