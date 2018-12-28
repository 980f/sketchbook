

#include "bitbanger.h"

#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"
#include "minimath.h"

#include "chainprinter.h"



/** merge usb serial and serial1 streams.*/
class TwinConsole {
  public:
    ChainPrinter dbg;
    ChainPrinter dbg2;
    TwinConsole(): dbg(Serial), dbg2(Serial1) {
      //#done unless we find we can call begin here.
    }

    int getKey() {
      if (Serial && Serial.available()) {
        return Serial.read();
      }
      if (Serial1 && Serial1.available()) {
        return Serial1.read();
      }
      return -1;
    }

    void begin() {
      Serial.begin(500000);//number here doesn't matter.
      Serial1.begin(500000);//hardware serial. up the baud to reduce overhead.
    }

    template<typename ... Args> TwinConsole& operator()(const Args ... args) {
      if (sizeof... (args)) {
        dbg(args...);
        dbg2(args...);
      }
      return *this;
    }

};

TwinConsole Console;
#define dbg Console

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
//9,10 PWM, in addition to being run to LED's
const OutputPin<9, LOW> T9;
const OutputPin<10, LOW> T10;
//we are doing these in geographic order, which after 10 is non-sequential
const OutputPin<16, LOW> T16;
const InputPin<14> fasterButton;
const InputPin<15> slowerButton;
//A0,A1,A2,A3


//joystick to servo version:
#include "analog.h"

template<typename T> struct XY {
  T X;
  T Y;
  XY(T x, T y): X(x), Y(y) {}
};

XY<AnalogInput> joy(A3, A2);

XY<AnalogValue> raw(0, 0);


struct LinearMap {
  const unsigned top;
  const unsigned bottom;
  LinearMap(unsigned top, unsigned bottom = 0):
    top(top), bottom(bottom)  {
    //#done
  }
  unsigned operator ()(AnalogValue avin)const {
    auto scaledup = long(top - bottom) * avin;
    unsigned reduced = bottom + ((scaledup + (1 << 14)) >> 15);
    //    dbg("\nLM:",top,"-",bottom, " in",avin, "\tup:", scaledup, "\tdone:", reduced);
    return reduced;
  }
};

//wanted to put these into Eyestalk but AVR compiler is too ancient for LinearMap init inline.
static const ProMicro::T1Control::CS cs = ProMicro::T1Control::By8;
static const unsigned fullscale = 40000;
static const LinearMap servoRange(4000, 2000); //from sparkfun: 20ms cycle and 1ms to 2ms range of signal.

struct Eyestalk {

  void begin() {
    board.T1.setPwmBase(fullscale, cs);
  }

  void X(AnalogValue value) {
    board.T1.pwmA.setDuty(servoRange(value));
  }

  void Y(AnalogValue value) {
    board.T1.pwmB.setDuty(servoRange(value));
  }

} eyestalk;

void showJoy() {
  dbg("\nJoy x:", raw.X, "\ty:", raw.Y);
}


void setup() {
  Console.begin();
  eyestalk.begin();
  dbg("\nHowdy, Podner\n\n\n");

}


bool readanalog = false;//enable joystick actions

void loop() {
  static unsigned iters = 0;
  ++iters;
  if (MilliTicked) { //this is true once per millisecond.
    if (fasterButton) {
      readanalog = false;
      board.T1.showstate(dbg.dbg2);
    }
    if (slowerButton) {
      readanalog = true;
    }

    T6 = readanalog;
    if (readanalog ) {
      T2.flip();
      raw.X = joy.X;
      raw.Y = joy.Y;
      if (MilliTicked.every(100)) {
        showJoy();
      }
      eyestalk.X(raw.X);
      eyestalk.Y(raw.Y);
    }

    if (MilliTicked.every(1000)) {
      T3.flip();
      Console(" @",MilliTicked.recent(),iters);
    }
  }


  int key = Console.getKey();
  if (key>=0) {
    T4.flip();
    Console(char(key), ':'); //echo.

    switch (key) {
      case 'p':
        showJoy();
        break;

      case 'i':
        board.ledd5 = 0;
        T5 = 0;
        break;
      case 'k':
        board.ledd5 = 1;
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
        //        if (!stepCLI(key)) {
        Console("?\n");
        //        }
        break;
      case '\n'://clear display via shoving some crlf's at it.
      case '\r':
        Console("\n\n\n");
        break;
    }
  }

}
