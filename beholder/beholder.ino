/**
Drive the Beholder.

We will have 6 labelled eyestalks, each can be killed, plus some that just wiggle until the beholder fully dies (as if)
So make that 6+1, where we might drive duplicate outputs for the one.

TODO: 
) get 16 channel pwm working perfectly.
) array of stalks, 0 for 'all' 1:6 for the featured ones.
) store calibration constants per stalk in eeprom.
) calibrator routine, pick a stalk , apply two pots to range, record 'center'.
) command parser to 
)) set 'waving', 'alert' common states, 
)) kill individual stalks. 
)) resurrect (reset for next group)
)) apply eyeball coords (a pair)
)) apply jaw actions (single analog channel)
)) apply eyebrows actions (single analog channel)

*/
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

#include "pca9685.h"
PCA9685 pwm;

//joystick to servo value:
#include "analog.h"
#include "linearmap.h"


//0,1 are rx,tx used by Serial1
//the test leds are low active.
//const OutputPin<2, LOW> T2;
//const OutputPin<3, LOW> T3;
//const OutputPin<4, LOW> T4;
//const OutputPin<5, LOW> T5;
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

template<typename T> struct XY {
  T X;
  T Y;
  XY(T x, T y):
    X(x), Y(y) {
    //#nada
  }//only works if class has default constructor


  XY() {
  }

  //type Other must be assignable to typename T
  template <typename Other>operator =(XY<Other> input) {
    X = input.X;
    Y = input.Y;
  }

};


//joystick device

XY<AnalogInput> joy(A1, A0);

//records recent joystick value
XY<AnalogValue> raw(0, 0);

//range adjustment knobs
AnalogInput lowend(A2);
AnalogInput highend(A2);

LinearMap servoRangeX(400, 200);
LinearMap servoRangeY(400, 200);

struct Eyestalk {
  XY<unsigned> adc;//4debug
  //  XY<LinearMap> servoRange;//each needs their own range, they vary.

  //XY<AnalogValue> dead(0, 0);
  //XY<AnalogValue> alert(0, 0);

  virtual void X(AnalogValue value) {

  }

  virtual  void Y(AnalogValue value);

  void operator =( XY<AnalogValue> raw) {
    X(raw.X);
    Y(raw.Y);
  }

} ;


struct FruitStalk:
  public Eyestalk {
  XY<uint8_t> which;//which of 16 servos. Allows for arbitrary pair

  FruitStalk(uint8_t xservo, uint8_t yservo):
    which(xservo, yservo) {

  }

  static void begin() {
    pwm.setPWMFreq(50);
    pwm.wake();
  }

  void X(AnalogValue value) {
    if (changed(adc.X, servoRangeX(value))) {
      pwm.setChannel(which.X , 0, adc.X);
    }
  }

  void Y(AnalogValue value) {
    if (changed(adc.Y, servoRangeY(value))) {
      pwm.setChannel(which.Y , 0, adc.Y);
    }
  }

  void operator =( XY<AnalogValue> raw) {
    X(raw.X);
    Y(raw.Y);
  }

};

//wanted to put these into Eyestalk but AVR compiler is too ancient for LinearMap init in line.
static const ProMicro::T1Control::CS cs = ProMicro::T1Control::By8;
static const unsigned fullscale = 40000; //40000 X 8 = 320000, /16MHz = 20ms aka 50Hz.
static const LinearMap T1ServoRange(4000, 2000); //from sparkfun: 20ms cycle and 1ms to 2ms range of signal.

struct T1Eyestalk:
  public Eyestalk {

  static void begin() {
    board.T1.setPwmBase(fullscale, cs);
  }

  void X(AnalogValue value)  {
    if (changed(adc.X, T1ServoRange(value))) {
      board.T1.pwmA.setDuty(adc.X);
    }
  }

  void Y(AnalogValue value) {
    if (changed(adc.Y, T1ServoRange(value))) {
      board.T1.pwmB.setDuty(adc.Y);
    }
  }

  void operator =( XY<AnalogValue> raw) {
    X(raw.X);
    Y(raw.Y);
  }
};

FruitStalk eyestalk0(0, 1);

T1Eyestalk eyestalk1;
void showRaw() {
  Console("\nPwm AF x: ", eyestalk0.adc.X, "\ty: ", eyestalk0.adc.Y);
  Console("\nPwm T1 x: ", eyestalk1.adc.X, "\ty: ", eyestalk1.adc.Y);
}

void showJoy() {
  Console("\nJoy x: ", raw.X, "\ty: ", raw.Y);
}

/** set adafruit pwm channels to their channel number, for debugging software and heck, devices as well with the 16 levels. */
void rampFruit(){  
  pwm.idChannels(0,15);  
}

////////////////////////////////////////////////////////////////
void setup() {
  //todo: figure out which of these is input, which is output,
  pinMode(1, INPUT_PULLUP); //RX is picking up TX on empty cable.
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.

  Console.begin();
  
  pwm.begin(4);//4:totempole drive.

  eyestalk0.begin();
  eyestalk1.begin();

  Console("\nSweeter 16 \n\n\n");
  rampFruit();

}

bool updateEyes = true; //enable joystick actions

void update(bool on) {
  if (changed(updateEyes, on)) {
    if (on) {
      Console("\nEnabling joystick");
    } else {
      Console("\nFreezing position");
    }
  }
}

void loop() {
  if (MilliTicked) { //this is true once per millisecond.
    if (updateEyes ) {
      raw = joy;//normalizes scale.
      if (MilliTicked.every(100)) {
        showJoy();
      }
      eyestalk0 = raw;
      eyestalk1 = raw;
    }

    if (MilliTicked.every(1000)) {
      Console(" @", MilliTicked.recent());
    }
  }


  int key = Console.getKey();
  if (key >= 0) {

    switch (key) {
      case 'l':
        update(false);
        break;
      case 'o':
        update(true);
        break;
      case 'p':
        showJoy();
        break;
      case 'x':
        showRaw();
        break;
      case 's':
        Console("\nScanning for I2C devices:");
        for (byte address = 16; address < 127; address++ )  {
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
              Console(".");
              break;
          }
        }
        Console("\nScanning for I2C devices is done.");
        break;

      default:
        Console("\nUnknown command:", key, ' ', char(key)); //echo.
        break;
      case '\n'://clear display via shoving some crlf's at it.
      case '\r':
        Console("\n\n\n");
        break;
    }
  }

}
