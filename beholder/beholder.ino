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

#include "scani2c.h"

//joystick to servo value:
#include "analog.h"
#include "linearmap.h"


//0,1 are rx,tx used by Serial1
//2,3 are I2C
//Andy's test leds are low active:
//const OutputPin<4, LOW> T4;
//const OutputPin<5, LOW> T5;
const OutputPin<6, LOW> T6;
//7-8 rotary knob
const InputPin<7, LOW> T7;
const InputPin<8, LOW> T8;
//9,10 PWM
const OutputPin<9, LOW> T9;
const OutputPin<10, LOW> T10;
//we are doing these in geographic order, which after 10 is non-sequential
const OutputPin<16, LOW> T16;
const InputPin<14> fasterButton;
const InputPin<15> slowerButton;

//A0,A1,A2,A3 used for joystick and pots.


void handleKnob();
#include "microevent.h"
/** a kinda quadrature encoder, a clock and direction */
template<unsigned clockPN, unsigned directionPN> class RotaryKnob {
    int location;

    const InputPin<clockPN, LOW> clock;
    const InputPin<directionPN, LOW> direction;

  public:
    RotaryKnob () {
      zero();

      //Arduino doesn't (yet;) support this:      auto handleKnob= [&](){update()};
      attachInterrupt(digitalPinToInterrupt(clockPN), &handleKnob, FALLING);
    }

    void zero() {
      location = 0;
    }

    void update() {
      location += direction ? -1 : 1;
    }
    /** getter for location */
    operator int()const {
      return location;
    }

};

RotaryKnob<7, 8> knob; //7 is only interrupt pin not otherwise occupied
void handleKnob() {
  knob.update();
}


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

const XY<const AnalogInput> joy(A1, A0);

//records recent joystick value
XY<AnalogValue> raw(0, 0);

//range adjustment knobs
const AnalogInput lowend(A2);
const AnalogInput highend(A3);

//400 to 200 gave 1.8ms to 3.75 ms, a factor of 1.875 or so off of expected.
LinearMap servoRangeX(400, 200);
LinearMap servoRangeY(400, 200);

struct Eyestalk {
  XY<unsigned> adc;//4debug
  //  XY<LinearMap> servoRange;//each needs their own range, they vary.

  //XY<AnalogValue> dead(0, 0);
  //XY<AnalogValue> alert(0, 0);

  virtual void X(AnalogValue value);

  virtual void Y(AnalogValue value);

  virtual void operator =( XY<AnalogValue> raw) {
    X(raw.X);
    Y(raw.Y);
  }

} ;

/** using adafruit 16 channel pwm controller
    we are initially leaving all the phases set to the same value, to minimize I2C execution time.
*/
struct FruitStalk:
  public Eyestalk {
  XY<uint8_t> which;//which of 16 servos. Allows for arbitrary pair

  FruitStalk(uint8_t xservo, uint8_t yservo):
    which(xservo, yservo) {
    //#done
  }

  void X(AnalogValue value) {
    if (changed(adc.X, servoRangeX(value))) {
      pwm.setWidth(which.X , adc.X);
    }
  }

  void Y(AnalogValue value) {
    if (changed(adc.Y, servoRangeY(value))) {
      pwm.setWidth(which.Y , adc.Y);
    }
  }

  using Eyestalk::operator =;

  static void begin() {
    pwm.begin(4, 50); //4:totempole drive.
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

  using Eyestalk::operator =;
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
void rampFruit() {
  pwm.idChannels(0, 15);
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

void joy2eye() {
  raw = joy;//normalizes scale.
  eyestalk0 = raw;
  eyestalk1 = raw;
}


////////////////////////////////////////////////////////////////
void setup() {
  T6 = 1;
  //todo: figure out which of these is input, which is output,
  pinMode(1, INPUT_PULLUP); //RX is picking up TX on empty cable.
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.

  Console.begin();
  Wire.begin();
  Wire.setClock(400000);//pca9685 device can go 1MHz, but 32U4 cannot.
  scanI2C();

  Console("\nConfiguring pwm eyestalk, divider =", pwm.fromHz(50));
  FruitStalk::begin();
  Console("\nConfiguring native eyestalk");
  T1Eyestalk::begin();

  Console("\nRamping pwm board outputs");
  rampFruit();
  //  Console("\nRamped pwm board outputs");
  T6 = 0;

  Console("\nBehold the Beholder 1.002 \n\n\n");
}


void loop() {
  if ( MilliTicked) { //this is true once per millisecond.
    if (updateEyes ) {
      joy2eye();
    }

    if (MilliTicked.every(1000)) {
      Console(" @", MilliTicked.recent());
    }
  }

  int key = Console.getKey();
  if (key >= 0) {
    switch (key) {
      case 'j':
        knob.zero();
      //#join
      case 'k':
        Console("\nKnob: ", knob);
        break;
      case ' ':
        joy2eye();
        showJoy();
        showRaw();
        break;
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
        scanI2C();
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
