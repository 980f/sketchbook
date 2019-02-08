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

#include "cheaptricks.h" //for changed()
#include "minimath.h"  //for template max/min instead of macro

#include "chainprinter.h" //for easier diagnostic messages

#include "stopwatch.h" //to time things

/** merge usb serial and serial1 streams.*/
#include "twinconsole.h"
TwinConsole Console;

#include "promicro.board.h" //mostly for 32U4 timer 1 use
ProMicro board;

#include "pca9685.h" //the 16 channel servo  controller
PCA9685 pwm;

#include "scani2c.h" //diagnostic scan for devices. the pc9685 shoes up as 64 and 112 on power up, I make the 112 go away.

//joystick to servo value:
#include "analog.h" //deal with the difference in number of bits of analog info in, out, and simulated  
#include "linearmap.h" //simpler scaling component than Arduino map(), also syntactically cuter.

//pin uses:
//0,1 are rx,tx used by Serial1
//2,3 are I2C bus (Wire)
const OutputPin<14, LOW> T4;
const OutputPin<15, LOW> T5;
const OutputPin<6, LOW> T6;
//7-8 rotary knob
//9,10 PWM outputs
const OutputPin<9, LOW> T9;
const OutputPin<10, LOW> T10;
//we are doing these in geographic order, which after 10 is non-sequential
const OutputPin<16, LOW> T16;
const InputPin<14, LOW> jawopen;
const InputPin<15, LOW> browup;

//A0,A1,A2,A3 used for joystick and pots.

#include "xypair.h"

//joystick device
const XY<const AnalogInput> joydev(A2, A3);
//pair of pots
const XY<const AnalogInput> potdev(A0, A1);


/** servo channel access and basic configuration such as range of travel */
class Muscle {
    /** using adafruit 16 channel pwm controller
        we are initially leaving all the phases set to the same value, to minimize I2C execution time.
       takes value in range 0:4095 */
    PCA9685::Output hw;
  public: //for debug access
    unsigned adc;//debug value: last sent to hw
  public:
    //temporarily shared for debug of code.
    LinearMap range;// = {400, 200}; //tweakable range, each unit needs a trim

    Muscle(PCA9685 &dev, uint8_t which): hw(dev, which), adc(~0), range(400, 200) {
      //#done
    }

    /** goto some position, as fraction of configured range. */
    void operator =(AnalogValue pos) {
      if (changed(adc, range(pos))) {
        hw = adc;
      }
    }

    bool test(unsigned raw) {
      if (changed(adc, range.clipped(raw))) {
        hw = adc;
        return true;
      } else {
        return false;
      }
    }

    /** @return pointer to related parameter, used by configuration interface */
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

    uint8_t which() {
      return hw.which;
    }
};

//temprarily shared range.
//LinearMap Muscle::range = {500, 10}; //tweakable range, each unit needs a trim

enum EyeState : uint8_t { //specing small type in case we shove it directly into eeprom
  Dead,
  Alert,
  Seeking
};
/** actuator with configuration for behaviors */
class EyeMuscle: public Muscle {
  public:
    /** position when related stalk is dead.*/
    AnalogValue dead = AnalogValue::Min; //will tweak for each stalk, for effect
    /** position when related stalk is under attack.*/
    AnalogValue alert = AnalogValue::Max; //will tweak for each stalk, for effect

    EyeMuscle(/*PCA9685 &dev,*/ uint8_t which): Muscle(pwm, which) {
      //#done
    }
    using Muscle::operator = ;

    void be(EyeState es) {
      switch (es) {
        case Dead:
          *this = dead;
          break;
        case Alert:
          *this = alert;
          break;
      }
    }

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




//application data

//records recent joystick value
XY<AnalogValue> joy(0, 0);

LinearMap highrange = {1000, 100};
LinearMap lowrange = {500, 1};
// the big eye is coded as eyestalk[0] so that we can share tuning and testing code.
EyeStalk eyestalk[1 + 6] = {//pwm channel numbers
  {0, 1},
  {2, 3},
  {4, 5},
  {6, 7},
  {8, 9},
  {10, 11},
  {12, 13},
};
//pairing jaw and eyebrow so that we can borrow eyestalk tuning and testing code
EyeStalk jawbrow(14, 15);
EyeMuscle &brow(jawbrow.muscle.Y);
EyeMuscle &jaw(jawbrow.muscle.X);


void showRaw() {
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {
    Console("\nPwm AF x: ", eyestalk[ei].muscle.X.adc, "\ty: ", eyestalk[ei].muscle.Y.adc);
  }
}

void showJoy() {
  Console("\nJoy x: ", joy.X, "\ty: ", joy.Y);
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

//all eyestalks in unison
void joy2all(XY<AnalogValue> xy) {
  for (unsigned ei = countof(eyestalk); ei-- > 1;) {//exclude big eyeball
    eyestalk[ei] = xy;
  }
}


//channel being tuned
short tunee = 0;
bool wm=0;// which muscle when tuning half of a pair

void joy2eye(XY<AnalogValue> xy) {
  eyestalk[tunee] = xy;
}

void setRange(unsigned topper, unsigned lower) {
  Muscle &muscle( wm ? eyestalk[tunee].muscle.X : eyestalk[tunee].muscle.Y);
  muscle.range.top = topper;
  muscle.range.bottom = lower;
  Console("\nRange: ", muscle.range.top, "->",  muscle.range.bottom);
}

void doarrow(bool plusone, bool upit) {
  wm=plusone;
  Muscle &muscle( wm ? eyestalk[tunee].muscle.X : eyestalk[tunee].muscle.Y);
  tweak(plusone, muscle.adc + upit ? 10 : -10);
}


void tweak(bool plusone, unsigned value) {
  wm=plusone;
  Muscle &muscle( wm? eyestalk[tunee].muscle.X : eyestalk[tunee].muscle.Y);
  muscle.test(value);
  Console("\npwm[", muscle.which(), "]=", muscle.adc);
}


#include "char.h"
#include "unsignedrecognizer.h"
UnsignedRecognizer param;
//settin up for 2 parameter commands
unsigned pushed;

#include "linearrecognizer.h"
LinearRecognizer ansicoder[2] = {"\e[", "\eO"};

//full keystroke echo, added for figuring out non-ascii keys.
bool rawecho = false;

/** made this key switch a function so that we can return when we have consumed the key versus some tortured 'exit if' */
void doKey(int key) {
  //test digits before ansi so that we can have a numerical parameter
  if (param(key)) { //part of a number, do no more
    return;
  }


  if (~ansicoder[0]) {//test and clear
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
      case '~'://a number appeared between escape prefix and the '~'
        switch (param) {
          case 3://del
          case 5://pageup
          case 6://page dn
            break;
        }
        break;
      default: //code not understood
        //don't treat unknown code as a raw one. esc [ x is not to be treated as just an x with the exception of esc itself
        if (!ansicoder[0](key)) {
          Console("\nUnknown ansi [ code:", char(key));
        }
        break;
    }
    return;
  }

  if (~ansicoder[1]) {//test and clear
    switch (key) {//ansi code
      case 'H':
        Console("\nWell Hello!");
        break;
      case 'F':
        Console("\nGoodbye!");
        break;
      case 'P'://F1..F4o
      case 'Q':
      case 'R':
      case 'S':
        Console("\nRomans:", key - 'P');
        break;
      //      case 'D':
      //        break;

      default: //code not understood
        //don't treat unknown code as a raw one. esc [ x is not to be treated as just an x with the exception of esc itself
        if (!ansicoder[1](key)) {
          Console("\nUnknown ansi O code:", char(key));
        }
        break;
    }
    return;
  }

  if (ansicoder[1](key) | ansicoder[0](key)) {//# must check all, return if any.
    return; //part of a prefix
  }

  switch (key) {
    case ','://push a parameter for 2 parameter commands.
      pushed = param;
      break;
    case '.'://simulate joystick value
      joy.X = take(pushed);
      joy.Y = param;
      joy2eye(joy);
      break;
    case 'F'://set pwm frequency parameter, 122 for 50Hz.
      Console("\n Set prescale to: ", param);
      pwm.setPrescale(param, true);
    //#join
    case 'f':
      Console("\n prescale: ", pwm.getPrescale());
      break;
    case 'w':
      tunee = param;
      Console("\nPair ", tunee);
      break;
    case 'x':
      tweak(0, param);
      break;
    case 's':
      tweak(1, param);
      break;
    case 'b':
      //      knob2range(0, param);
      break;
    case 't':
      //      knob2range(1, param);
      break;
    case 'r':
      if (pushed) { //zero is not a reasonable range value so we can key off this
        setRange( take(pushed),param);
      }
      break;
    case 'p':
      showJoy();
      break;
    case 'j':
      showRaw();
      break;
    case ' ':
      joy2eye(joy);
      showJoy();
      showRaw();
      //      showMrange();
      break;
    case 'l'://disconnect eyes from joystick
      update(false);
      break;
    case 'o'://run eyes from joystick
      update(true);
      break;
    case '*':
      scanI2C();
      break;
    case '!'://debug function key input
      rawecho = param;
      Console("\nraw echo:", rawecho);
      break;
    case '@'://set each servo output to a value computed from its channel #
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

////////////////////////////////////////////////////////////////
void setup() {
  T6 = 1;
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.

  Console.begin();

  Wire.begin(); //must preceded scan!
  Wire.setClock(400000);//pca9685 device can go 1MHz, but 32U4 cannot.
  scanI2C();

  Console("\nConfiguring pwm eyestalk, divider =", 1 + pwm.fromHz(50));
  amMonster = pwm.begin(4, 50); //4:totempole drive.
  T6 = 0;

  Console("\n", amMonster ? "Behold" : "Where is", " the Beholder 1.005\n\n\n");
}


void loop() {
  if (MilliTicked) { //this is true once per millisecond.
    joy = joydev;//normalizes scale.
    if (updateEyes) {
      joy2eye(joy);
    }
    brow.be(browup ? Alert : Dead);
    jaw.be(jawopen ? Alert : Dead);
  }

  int key = Console.getKey();
  if (key >= 0) {
    if (rawecho) {
      Console("\tKey: ", key, ' ', char(key));
    }
    doKey(key) ;
  }
}
