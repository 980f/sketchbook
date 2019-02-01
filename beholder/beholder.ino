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
//9,10 PWM
const OutputPin<9, LOW> T9;
const OutputPin<10, LOW> T10;
//we are doing these in geographic order, which after 10 is non-sequential
const OutputPin<16, LOW> T16;
const InputPin<14> fasterButton;
const InputPin<15> slowerButton;

//A0,A1,A2,A3 used for joystick and pots.


void handleKnob_knob();
/** a kinda quadrature encoder, a clock and direction. The detents are only where both signals are high, most likely to ensure low power consumption.
    The data type must be atomic if you are going to read it raw. When we fin
*/
template<unsigned clockPN, unsigned directionPN, typename Knobish = uint8_t> class RotaryKnob {
    Knobish location;

    const InputPin<clockPN, LOW> clock;
    const InputPin<directionPN, LOW> direction;

  public:
    RotaryKnob () {
      zero();
      //Arduino doesn't (yet;) support this:      auto handleKnob= [&](){update()};
      attachInterrupt(digitalPinToInterrupt(clockPN), &handleKnob_knob, FALLING);
    }

    void zero() {
      location = 0;
    }

    void update() {
      location += direction ? -1 : 1;
    }
    /** getter for location */
    operator Knobish()const {
      return location;
    }

};

RotaryKnob<7, 8> knob; //7 is only interrupt pin not otherwise occupied
void handleKnob_knob() {
  knob.update();
}


template<typename T> struct XY {
  T X;
  T Y;


  /**only works if class has default constructor:*/
  XY() {
    //#done
  }

  /** single arg constructor */
  template<typename C>
  XY(C x, C y):
    X(x), Y(y) {
    //#nada
  }

  /** two arg constructor */
  template<typename C1, typename C2>
  XY(C1 x1, C2 x2, C1 y1, C2 y2):
    X(x1, x2), Y(y1, y2) {
    //#nada
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


class Muscle {
    /** takes value in range 0:4095 */
    PCA9685::Output hw;
  public: //for debug access
    unsigned adc;//debug value: last sent to hw
  public:
    LinearMap range = {400, 200}; //tweakable range, each unit needs a trim

    Muscle(PCA9685 &dev, uint8_t which): hw(dev, which), adc(~0) {
      //#done
    }

    void operator =(AnalogValue pos) {
      if (changed(adc, range(pos))) {
        hw = adc;
      }
    }

    /** @return pointer to related parameter */
    virtual uint16_t *param(char key) {
      switch (key) {
        case 'b':
          return &range.bottom;
        case 't':
          return &range.top;
        default:
          return nullptr;
      }
    }
};


class EyeMuscle: public Muscle {

  public:
    /** position when related stalk is dead.*/
    AnalogValue dead = 0; //will tweak for each stalk, for effect
    /** position when related stalk is under attack.*/
    AnalogValue alert = 0x7FFF; //will tweak for each stalk, for effect

    EyeMuscle(/*PCA9685 &dev,*/ uint8_t which): Muscle(pwm, which) {
      //#done
    }
    using Muscle::operator = ;

    /** @return pointer to related parameter */
    uint16_t *param(char key) {
      switch (key) {
        case 'd':
          return dead.guts();
        case 'a':
          return alert.guts();
        default:
          return Muscle::param(key);
      }
    }
};



/** using adafruit 16 channel pwm controller
    we are initially leaving all the phases set to the same value, to minimize I2C execution time.
*/
struct EyeStalk {
  XY<EyeMuscle> muscle;

  EyeStalk(uint8_t xservo, uint8_t yservo):
    muscle(xservo, yservo) {
    //#done
  }

  void operator=(XY<AnalogValue> joystick) {
    muscle = joystick;
  }

};

////wanted to put these into Eyestalk but AVR compiler is too ancient for LinearMap init in line.
//static const ProMicro::T1Control::CS cs = ProMicro::T1Control::By8;
//static const unsigned fullscale = 40000; //40000 X 8 = 320000, /16MHz = 20ms aka 50Hz.
//static const LinearMap T1ServoRange(4000, 2000); //from sparkfun: 20ms cycle and 1ms to 2ms range of signal.
//
//struct T1Eyestalk:
//  public Eyestalk {
//
//  static void begin() {
//    board.T1.setPwmBase(fullscale, cs);
//  }
//
//  void X(AnalogValue value)  {
//    if (changed(adc.X, T1ServoRange(value))) {
//      board.T1.pwmA.setDuty(adc.X);
//    }
//  }
//
//  void Y(AnalogValue value) {
//    if (changed(adc.Y, T1ServoRange(value))) {
//      board.T1.pwmB.setDuty(adc.Y);
//    }
//  }
//
//  using Eyestalk::operator =;
//};

EyeStalk eyestalk[1 + 6] = {
  {0, 1},
  {2, 3},
  {4, 5},
  {6, 7},
  {8, 9},
  {10, 11},
  {12, 13},
};

//T1Eyestalk eyestalk1;

void showRaw() {
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {
    Console("\nPwm AF x: ", eyestalk[ei].muscle.X.adc, "\ty: ", eyestalk[ei].muscle.Y.adc);
  }
}

void showJoy() {
  Console("\nJoy x: ", raw.X, "\ty: ", raw.Y);
}


bool updateEyes = true; //enable joystick actions
bool amMonster = false; //detects which end of link

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
  eyestalk[0] = raw;

}


////////////////////////////////////////////////////////////////
void setup() {
  T6 = 1;
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.

  Console.begin();
  
  Wire.begin(); //must preceded scan!
  Wire.setClock(400000);//pca9685 device can go 1MHz, but 32U4 cannot.
  scanI2C();

  Console("\nConfiguring pwm eyestalk, divider =", pwm.fromHz(50));  
  amMonster=pwm.begin(4, 50); //4:totempole drive.
  T6 = 0;
  Console("\n",amMonster?"Behold":"Who is"," the Beholder 1.003 \n\n\n");
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
