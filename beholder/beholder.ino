/**
  Drive the Beholder.

  We will have 6 labelled eyestalks, each can be killed, plus some that just wiggle until the beholder fully dies (as if)
  So make that 6+1, where we might drive duplicate outputs for the one.

  TODO:
  ) get 16 channel pwm working perfectly.
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

#if knobby
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

#endif

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


class Muscle {
    /** takes value in range 0:4095 */
    PCA9685::Output hw;
  public: //for debug access
    unsigned adc;//debug value: last sent to hw
  public:
    //temporarily shared for debug of code.
    static LinearMap range;// = {400, 200}; //tweakable range, each unit needs a trim

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

LinearMap Muscle::range = {500, 10}; //tweakable range, each unit needs a trim

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


EyeStalk eyestalk[1 + 6] = {
  {0, 1},
  {2, 3},
  {4, 5},
  {6, 7},
  {8, 9},
  {10, 11},
  {12, 13},
};

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
  //  raw = joy;//normalizes scale.
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {
    eyestalk[ei] = raw;
  }
}

void knob2range(bool upper, unsigned asis) {
  if (upper) {
    Muscle::range.top = asis;
  } else {
    Muscle::range.bottom = asis;
  }
  Console("\nMuscle: ", Muscle::range.bottom, ":", Muscle::range.top);
}

//range adjustment knobs
const AnalogInput lowend(A2);
const AnalogInput highend(A3);

LinearMap highrange = {1000, 100};
LinearMap lowrange = {500, 1};

void showMrange() {
  Console("\nMuscle: ", Muscle::range.bottom, ":", Muscle::range.top);
}

//channel being tuned
uint8_t tunee = 0;

#include "char.h"

////////////////////////////////////////////////////////////////
void setup() {
  T6 = 1;
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.

  Console.begin();

  Wire.begin(); //must preceded scan!
  Wire.setClock(400000);//pca9685 device can go 1MHz, but 32U4 cannot.
  scanI2C();

  Console("\nConfiguring pwm eyestalk, divider =", pwm.fromHz(50));
  amMonster = pwm.begin(4, 50); //4:totempole drive.
  T6 = 0;

  Console("\n", amMonster ? "Behold" : "Where is", " the Beholder 1.005\n\n\n");
}



uint16_t loc[2];

void tweak(bool plusone, unsigned value) {
  loc[plusone] = Muscle::range.clipped(value);
  pwm.setWidth(tunee + plusone, value);
  Console("\npwm[", tunee + plusone, "]=", value);
}

/** a cheap sequence recognizer.
    present it with your tokens, it returns whether the sequence might be present.
    the ~operator reports on whether the sequence was recognized, and if so then forgets that it was.
    the operator bool reports on whether the complete sequence has been recognized, it is cleared when presented another token so must be checked with each token.
*/
class LinearRecognizer {
    const char * const seq;
    short si = 0; //Arduino optimization

  public:
    LinearRecognizer(const char * const seq): seq(seq) {}

    /** @returns whether sequence has been seen */
    operator bool()const {
      return seq[si] == 0;
    }

    /** @returns <em>false</em> if sequence has been recognized, in which case recognition is forgotten. */
    bool operator ~() {
      if (this->operator bool()) {
        si = 0;
        return false;
      } else {
        return true;
      }
    }

    /** @returns whether @param next was expected part of sequence */
    bool operator ()(char next) {
      if (next == seq[si]) {
        ++si;
        return true;
      } else {
        si = 0;
        return false;
      }
    }
};

struct NumberRecognizer {
  unsigned accumulator = 0;
  bool operator()(int key) {
    if (Char(key).appliedDigit(accumulator)) { //rpn number entry
      //      Console("\n",accumulator);
      return true;
    } else {
      return false;
    }
  }
  /** look at it and it is gone! */
  operator unsigned() {
    return take(accumulator);
  }
};

void doarrow(bool plusone, bool upit) {
  loc[plusone] += upit ? 10 : -10;
  Muscle::range.clip(loc[plusone]);
  unsigned value = loc[plusone];
  pwm.setWidth(tunee + plusone, value);
  Console("\npwm[", tunee + plusone, "]=", value);
}

LinearRecognizer ansicoder("\e[");
NumberRecognizer param;

/** made this key switch a function so that we can return when we have consumed the key versus some tortured 'exit if' */
void doKey(int key) {
  //test digits before ansi so that we can have a numerical parameter
  if (param(key)) { //part of a number, do no more
    return;
  }

  if (~ansicoder) {//test and clear
    switch (key) {//ansi code
      case 'A':
        doarrow(1, 1);
        break;
      case 'B':
        doarrow(1, 0);
        break;
      case 'C':
        doarrow(0, 1);
        break;
      case 'D':
        doarrow(0, 0);
        break;

      default: //code not understood
        //don't treat unknown code as a raw one. esc [ x is not to be treated as just an x with the exception of esc itself
        if (!ansicoder(key)) {
          Console("\nUnknown ansi code:", char(key));
        }
        break;
    }
    return;
  }

  if (ansicoder(key)) {
    return; //part of a prefix
  }


  switch (key) {
    case 'F':
      Console("\n Set prescale to: ", param);
      pwm.setPrescale(param, true);
    //#join
    case 'f':
      Console("\n prescale: ", pwm.getPrescale());
      break;
    case 'w':
      tunee = 2 * param;
      break;
    case 'x':
      tweak(0, param);
      break;
    case 's':
      tweak(1, param);
      break;
    case 'b':
      knob2range(0, param);
      break;
    case 't':
      knob2range(1, param);
      break;
    case ' ':
      joy2eye();
      showJoy();
      showRaw();
      showMrange();
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
    case 'j':
      showRaw();
      break;
    case '*':
      scanI2C();
      break;
    case 'r':
      pwm.idChannels(2, 15);
      Console("\npwm's are now set to their channel number");
      break;
    default:
      Console("\nUnknown command:", key, ' ', char(key));
      break;
    case '\n'://clear display via shoving some crlf's at it.
    case '\r':
      Console("\n\n\n");
      break;
  }
}


void loop() {
  if ( MilliTicked) { //this is true once per millisecond.
    raw = joy;//normalizes scale.
    if (updateEyes ) {
      joy2eye();
    }
  }

  int key = Console.getKey();
  if (key >= 0) {
    doKey(key) ;
  }
}
