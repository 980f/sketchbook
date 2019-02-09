/**
  Drive the Beholder.

  We will have 6 labelled eyestalks, each can be killed, plus some that just wiggle until the beholder fully dies (as if)
  So make that 6+1, where we might drive duplicate outputs for the one.

  TODO:
  ) store calibration constants per stalk in eeprom.
  ) calibrator routine, pick a stalk , apply two pots to range, record 'center'.
  ) command parser to
  )) set 'waving', 'alert' common states,
  )) kill individual stalks.
  )) resurrect (reset for next group)

  NOTE WELL: The value ~0 is used to mark 'not a value'. If not checked then it is either -1 or 65535 aka 'all ones'.

*/

#include "bitbanger.h" //early to replace Arduino's macros with real code
#include "cheaptricks.h" //for changed()
#include "minimath.h"  //for template max/min instead of macro

#include "stopwatch.h" //to time things

#include "pinclass.h"
//#include "digitalpin.h"
#include "millievent.h"

/** merge usb serial and serial1 streams.*/
#include "twinconsole.h"
TwinConsole Console;

#include "scani2c.h" //diagnostic scan for devices. the pc9685 shows up as 64 and 112 on power up, I make the 112 go away which conveniently allows us to distinguish reset from power cycle.
#include "pca9685.h" //the 16 channel servo  controller
PCA9685 pwm;

//joystick to servo value:
#include "analog.h" //deal with the difference in number of bits of analog info in, out, and simulated  
#include "linearmap.h" //simpler scaling component than Arduino map(), also syntactically cuter.

#include "xypair.h"

//pin uses:
//0,1 are rx,tx used by Serial1
//2,3 are I2C bus (Wire)
const OutputPin<14, LOW> T4;
const OutputPin<15, LOW> T5;
const OutputPin<6, LOW> T6;
//7-8 reserved for rotary knob
//9,10 PWM outputs
const OutputPin<9, LOW> T9;
const OutputPin<10, LOW> T10;
//we are doing these in geographic order, which after 10 is non-sequential
const OutputPin<16, LOW> T16;

//digital inputs to control eyebrow and jaw:
const InputPin<14, LOW> jawopen;
const InputPin<15, LOW> browup;

//A0,A1,A2,A3 used for joystick and pots.
//joystick device
const XY<const AnalogInput> joydev(A2, A3);
//pair of pots
const XY<const AnalogInput> potdev(A0, A1);


/** servo channel access and basic configuration such as range of travel */
struct Muscle {
  unsigned adc;//debug value: last sent to hw
  AnalogValue pos;
  /** using adafruit 16 channel pwm controller
      we are initially leaving all the phases set to the same value, to minimize I2C execution time.
     takes value in range 0:4095 */
  PCA9685::Output hw;
  LinearMap range; //tweakable range, trimming the servo.

  Muscle(PCA9685 &dev, uint8_t which): adc(~0), hw(dev, which), range(400, 200) {
    //#done
  }

  /** easy creator, but comipler wants to create copies for assignment and gets confused with operator = without the 'explicit' */
  explicit Muscle(uint8_t which): Muscle(pwm, which) {}

  /** goto some position, as fraction of configured range. */
  void operator =(AnalogValue pos) {
    if (changed(adc, range(this->pos = pos))) {
      hw = adc;
    }
  }

  /** access pwm and set it to a raw value, within range of this muscle.*/
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

};

/** each stalk is in one of these states */
enum EyeState : uint8_t { //specing small type in case we shove it directly into eeprom
  Dead,   // as limp appearing as possible
  Alert,  // looking at *you*
  Seeking // wiggling aimlessly
};

/** actuator to use */
using EyeMuscle = Muscle ;

struct EyeStalk : public XY<EyeMuscle> {
  EyeState es = ~0; //for behavior stuff, we will want to do things like "if not dead then"
  EyeStalk (short xw, short yw): XY<EyeMuscle>(xw, yw) {}

  /** set to a pair of values */
  using XY<EyeMuscle>:: operator =;
  /** position when related stalk is dead.*/
  XY<AnalogValue> dead = {AnalogValue::Min, AnalogValue::Min}; //will tweak for each stalk, for effect
  /** position when related stalk is under attack.*/
  XY<AnalogValue> alert = {AnalogValue::Max, AnalogValue::Max}; //will tweak for each stalk, for effect

  void be(EyeState es) {
    if (changed(this->es, es)) {
      switch (es) {
        case Dead:
          *this = dead;
          break;
        case Alert:
          *this = alert;
          break;
        default:
          break;
      }
    }
  }

};


//application data

//records recent joystick value
XY<AnalogValue> joy(0, ~0);//weird init so we can detect 'never init'

void showJoy() {
  Console("\nJoy x: ", joy.X.raw, "\ty: ", joy.Y.raw);
}

// the big eye is coded as eyestalk[0] so that we can share tuning and testing code.
EyeStalk eyestalk[] = {//pwm channel numbers
  {0, 1},
  {2, 3},
  {4, 5},
  {6, 7},
  {8, 9},
  {10, 11},
  {12, 13},
  {14, 15}
};
//pairing jaw and eyebrow so that we can borrow eyestalk tuning and testing code

EyeMuscle &brow(eyestalk[7].Y);
EyeMuscle &jaw(eyestalk[7].X);

//show all pwm outputs
void showRaw() {
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {
    Console("\nPwm AF x: ", eyestalk[ei].X.adc, "\ty: ", eyestalk[ei].Y.adc);
  }
}

//UI state
struct UI {
  bool updateEyes = true; //enable joystick actions
  bool amMonster = false; //detects which end of link

  //full keystroke echo, added for figuring out non-ascii keys.
  bool rawecho = false;

  //channel being tuned
  short tunee = 0;
  bool wm = 0; // which muscle when tuning half of a pair

  //muscle of interest
  Muscle &moi() const {
    Muscle &muscle( wm ? eyestalk[tunee].X : eyestalk[tunee].Y);
    return muscle;
  }

  void update(bool on) {
    if (changed(updateEyes, on)) {
      if (on) {
        Console("\nEnabling joystick");
      } else {
        Console("\nFreezing position");
      }
    }
  }

} ui;

//all eyestalks in unison
void joy2all(XY<AnalogValue> xy) {
  for (unsigned ei = 6; ei-- > 1;) {//exclude big eyeball, brow and jaw
    eyestalk[ei] = xy;
  }
}

void joy2eye(XY<AnalogValue> xy) {
  eyestalk[ui.tunee] = xy;
}


void setRange(unsigned topper, unsigned lower) {
  Muscle &muscle( ui.moi());
  muscle.range = LinearMap(topper, lower);
  Console("\nRange: ", muscle.range.top, "->",  muscle.range.bottom);
}


void tweak(bool plusone, unsigned value) {
  ui.wm = plusone;
  Muscle &muscle( ui.moi());
  muscle.test(value);
  Console("\npwm[", muscle.hw.which, "]=", muscle.adc);
}

void doarrow(bool plusone, bool upit) {
  ui.wm = plusone;
  Muscle &muscle( ui.moi());
  tweak(plusone, muscle.adc + upit ? 10 : -10);
}

void be(EyeState es) {
  eyestalk[ui.tunee].be(es);
}

/** we always go to the position to record, then record it. So we can reuse the position request variable as the value to save*/
void record(EyeState es) {
  switch (es) {
    case Dead:
      eyestalk[ui.tunee].dead = joy;
      break;
    case Alert:
      eyestalk[ui.tunee].alert = joy;
    default:
      //Console("\nBad eyestate in record",es);
      break;
  }
}

///////////////////////////////////////////////////////////////

/** this data will eventually come from EEPROM.
    this code takes advantage of the c compiler concatenating adjacent quote delimited strings into one string.
*/
const char initdata[] = {

  //channel.w.range.x.range.y.position.dead.position.alert
  "1w400,200x400,200y0,0D20000,20000A"
  "2w400,200x400,200y0,0D20000,20000A"
  "3w400,200x400,200y0,0D20000,20000A"
  "4w400,200x400,200y0,0D20000,20000A"
  "5w400,200x400,200y0,0D20000,20000A"
  "6w400,200x400,200y0,0D20000,20000A"
  "0w400,200x400,200y0,0D20000,20000A" //big eye
  "7w400,200x400,200y0,0D20000,20000A" //jawbrow

};

void processinit() {
  Console("\nInit block is ", sizeof(initdata), " bytes");
  const char *ptr = initdata;
  while (char c = *ptr++) {
    doKey(int(c));
  }
  Console("\nInit block done.");
}

////////////////////////////////////////////////////////////////
#include "unsignedrecognizer.h"
UnsignedRecognizer param;
//settin up for 2 parameter commands
unsigned pushed;

#include "linearrecognizer.h"
LinearRecognizer ansicoder[2] = {"\e[", "\eO"};

void setJoy() {
  joy.X = take(pushed);
  joy.Y = param;
  joy2eye(joy);
}

/** either record the given position as a the given state, or go into that state */
void doSetpoint(boolean set, EyeState es ) {
  if (set) {
    if (param && pushed) {
      setJoy();//goes to entered position
    }
    record(es);
  } else {//goto dead position
    if (param) {
      ui.tunee = param;
    }
  }
  be(es);
}


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
      case 'P'://F1..F4
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
      setJoy();
      break;

    case 'D': //record dead position
      doSetpoint(true, EyeState::Dead);
      break;
    case 'd': //goto dead position
      doSetpoint(false, EyeState::Dead);
      break;

    case 'A': //record alert position
      doSetpoint(true, EyeState::Alert);
      break;
    case 'a': //goto alert position
      doSetpoint(false, EyeState::Alert);
      break;

    case 'F'://set pwm frequency parameter, 122 for 50Hz.
      Console("\n Set prescale to: ", param);
      pwm.setPrescale(param, true);
    //#join
    case 'f':
      Console("\n prescale: ", pwm.getPrescale());
      break;

    case 'w': //select eye of interest
      ui.tunee = param;
      Console("\nEye ", ui.tunee);
      break;

    case 'e':
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
    case 'x': //pick axis, if values present then set its range.
    case 'y':
      ui.wm = key & 1;
      if (pushed) { //zero is not a reasonable range value so we can key off this
        setRange( take(pushed), param);
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
      ui.update(false);
      break;
    case 'o'://run eyes from joystick
      ui.update(true);
      break;
    case '*':
      scanI2C();
      break;
    case '!'://debug function key input
      ui.rawecho = param;
      Console("\nraw echo:", ui.rawecho);
      break;
    case 'I':
      processinit();
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
  ui.amMonster = pwm.begin(4, 50); //4:totempole drive.
  T6 = 0;

  //running the init block:
  //  processinit();
  Console("\n", ui.amMonster ? "Behold" : "Where is", " the Beholder (bin: 8feb2019 19:09)\n\n\n");//todo: git hash insertion.
}


void loop() {
  if (MilliTicked) { //this is true once per millisecond.
    joy = joydev;//normalizes scale.
    if (ui.updateEyes) {
      joy2eye(joy);
    }
    brow = (browup ? AnalogValue::Max : AnalogValue::Min);
    jaw = (jawopen ? AnalogValue::Max : AnalogValue::Min);
  }

  int key = Console.getKey();
  if (key >= 0) {
    if (ui.rawecho) {
      Console("\tKey: ", key, ' ', char(key));
    }
    doKey(key) ;
  }
}
