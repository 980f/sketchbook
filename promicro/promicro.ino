
//set useFruit to 1 to use adafruit 16 channel system as output (4k resolution), 0 for AVR TIM1 on D9/D10 as PWM pair with 40k resolution.
#define useFruit 1


#include "bitbanger.h"

#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"
#include "minimath.h"

#include "chainprinter.h"

#include "stopwatch.h"

/** merge usb serial and serial1 streams.*/
#include "twinconsole.h"
TwinConsole Console;


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


//joystick to servo value:
#include "analog.h"

template<typename T> struct XY {
  T X;
  T Y;
  XY(T x, T y): X(x), Y(y) {}

  //type Other must be assignable to typename T
  template <typename Other>operator =(XY<Other> input) {
    X = input.X;
    Y = input.Y;
  }

};


//joystick device

XY<AnalogInput> joy(A3, A2);
//records recent joystick value
XY<AnalogValue> raw(0, 0);

/** like arduino's map() but with subtle syntax and one axis with a fixed 0 as the low.*/
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


#include <Adafruit_PWMServoDriver.h>
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#if useFruit

//4k range
static const LinearMap servoRange(410, 205); //from sparkfun: 20ms cycle and 1ms to 2ms range of signal.

struct Eyestalk {

  static void begin() {
    pwm.setPWMFreq(50);  // Analog servos run at ~60 Hz updates
    //25MHz is base clock, cycle is 4k ticks so max rate is
  }

  void X(AnalogValue value) {
    pwm.setPWM(which.X , 0, servoRange(value));
  }

  void Y(AnalogValue value) {
    pwm.setPWM(which.Y , 0, servoRange(value));
  }

  XY<uint8_t> which = {0, 1};//which of 16 servos. Allows for arbitrary pair

  void operator =(const XY<AnalogValue>&raw) {
    X(raw.X);
    Y(raw.Y);
  }

} eyestalk;
#else
//wanted to put these into Eyestalk but AVR compiler is too ancient for LinearMap init in line.
static const ProMicro::T1Control::CS cs = ProMicro::T1Control::By8;
static const unsigned fullscale = 40000; //40000 X 8 = 320000, /16MHz = 20ms aka 50Hz.
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

  void operator =(const XY<AnalogValue>&raw) {
    X(raw.X);
    Y(raw.Y);
  }

} eyestalk;
#endif

void showJoy() {
  Console("\nJoy x:", raw.X, "\ty:", raw.Y);
}


void setup() {
  Console.begin();
  pwm.begin();//use by eyestalks, should precede use of them.

  eyestalk.begin();
  Console("\nSweet 16 \n\n\n");
  //todo: figureout which is input, which is output,
  pinMode(1, INPUT_PULLUP); //RX is picking up TX on empty cable.
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.
  //put power to pins to test w/voltmeter.
  for (unsigned pi = 16; pi-- > 0;) {
    pwm.setPWM(pi, 0, pi << 8);
  }

}

bool updateEyes = false; //enable joystick actions

void loop() {
  //  static unsigned iters = 0;
  //  ++iters;
  //  Console("\n!", iters, ",", millis());
  //  Console("\n#", iters, ",", millis());

  if (MilliTicked) { //this is true once per millisecond.
    //    Console("\n+", MilliTicked.recent());
    if (fasterButton) {
      if (changed(updateEyes, false)) {
        Console("\nDisabling joystick");
      }
      board.T1.showstate(Console.uart);
    }
    if (slowerButton) {
      if (changed(updateEyes, true)) {
        Console("\nEnabling joystick");
      }
    }

    T6 = updateEyes;
    if (updateEyes ) {
      T2.flip();
      raw = joy;//normalizes scale.
      if (MilliTicked.every(100)) {
        showJoy();
      }
      eyestalk = raw;
    }

    if (MilliTicked.every(1000)) {
      T3.flip();
      Console(" @", MilliTicked.recent());
    }
  }


  int key = Console.getKey();
  if (key >= 0) {
    T4.flip();
    Console(key, ' ', char(key), ':'); //echo.

    switch (key) {
      case 'p':
        showJoy();
        break;
      case 's':
        Console("\nScanning for I2C devices:");
        for (byte address = 16; address < 127; address++ )  {
          Console(".");
          Wire.beginTransmission(address);
          auto error = Wire.endTransmission();
          switch (error) {
            case 0:
              Console("\nI2C device found at address ", address);
              break;
            case 4:
              Console("\nUnknown error for address ", address);
              break;
            default:
              Console(error);
              break;
          }
        }
        Console("\nScanning for I2C devices is done.");
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
