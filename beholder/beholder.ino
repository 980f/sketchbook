/**
  Drive the Beholder.

  We will have 6 labelled eyestalks, each can be killed, plus some that just wiggle until the beholder fully dies (as if)
  So make that 6+1, where we might drive duplicate outputs for the one.

  TODO:
  ) store calibration constants per stalk in eeprom.  (putting into program source for now as stored command sequences)
  ) calibrator routine, pick a stalk , apply two pots to range, record 'center'.

  NOTE WELL: The value ~0 is used to mark 'not a value'. If not checked then it is either -1 or 65535 aka 'all ones'.
  NOTE WELL: Using a preceding ~ on some psuedo variables is used to deal with otherwise ambiguous cast overloads. Sorry.

*/
#define REVISIONMARKER "2019feb14-21:37"

///////////////////////////////////////////////////////////////
//this chunk takes advantage of the c compiler concatenating adjacent quote delimited strings into one string.
const PROGMEM char initblock[] =
  //channel.w.range.x.range.y.position.dead.position.alert
  ":" //enable tuning commands
  "\n	1w	400,200x	400,200y	1,2D	20000,20001A"  //using ls digit as tracer for program debug.
  "\n	2w	400,200x	400,200y	2,3D	20000,20002A"
  "\n	3w	400,200x	400,200y	3,2D	20000,20003A"
  "\n	4w	400,200x	400,200y	4,5D	20000,20004A"
  "\n	5w	400,200x	400,200y	5,2D	20000,20005A"
  "\n	6w	400,200x	400,200y	6,9D	20000,20006A"
  "\n	0w	400,200x	400,200y	0,0D	20000,20000A" //big eye
  "\n	7w	400,200x	400,200y	7,6D	20000,20007A" //jawbrow
  "\n	500h	24000H"  //wiggler config rate then yamp
  ;
#include "initer.h"
void doKey(byte key);
Initer Init(RomAddr(initblock), doKey);

/////////////////////////////////////////////////////////////

#include "bitbanger.h" //early to replace Arduino's macros with real code
#include "minimath.h"  //for template max/min instead of macro
#include "cheaptricks.h" //for changed()

//#include "stopwatch.h" //to time things

#include "pinclass.h"
//#include "digitalpin.h"
#include "millievent.h"

/** merge usb serial and serial1 streams.*/
#include "twinconsole.h"
TwinConsole Console;

/** streams kept separate for remote controller, where we do cross the streams. */
#include "easyconsole.h"
EasyConsole<decltype(Serial)> Local(Serial);
EasyConsole<decltype(Serial1)> Remote(Serial1);

//marker for codespace strings, with newline prefixed. With this or the Arduino provided F() constr strings take up ram as well as rom.
#define FF(farg)  F( "\n" farg)

#include "scani2c.h" //diagnostic scan for devices. the pc9685 shows up as 64 and 112 on power up, I make the 112 go away in setup() which conveniently allows us to distinguish reset from power cycle.
#include "pca9685.h" //the 16 channel servo  controller
PCA9685 pwm;


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

//jumper this to select program mode: if jumpered low then is connected to UI and sends commands to device which actually runs the beast.
const InputPin<16, HIGH> BeMonster;

//digital inputs to control eyebrow and jaw:
const InputPin<14, LOW> jawopen;
const InputPin<15, LOW> browup;

//A0,A1,A2,A3 used for joystick and pots.


//joystick to servo value:
#include "analog.h" //deals with the difference in number of bits of analog info in, out, and simulated  
#include "linearmap.h" //simpler scaling component than Arduino map(), also syntactically cuter.

#include "xypair.h"  //the eyestalks are 2 dimensional

//joystick device
const XY<const AnalogInput> joydev(A2, A3);
//pair of pots
const XY<const AnalogInput> potdev(A0, A1);

/** servo channel access and basic configuration such as range of travel */
struct Muscle: public Printable {
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

  /** easy creator, but compiler wants to create copies for assignment and gets confused with operator = without the 'explicit' */
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

  size_t printTo(Print& p) const {
    return p.print(~pos);
  }

};

/** each stalk is in one of these states */
enum EyeState : uint8_t { //specing small type in case we shove it directly into eeprom
  //#order matters as it is used as an array index, add new stuff to the end.
  Dead,   // as limp appearing as possible
  Alert,  // looking at *you*
  Seeking // wiggling aimlessly
};

/** A 2D coordinate value. */
using Gaze = XY<AnalogValue>;

/** actuator to use */
using EyeMuscle = Muscle;

/*init line:
  "\n	1w	400,200x	400,200y	1,2D	20000,20001A"
  .[X,Y].range.top,range.bottom [x,y]
  ~pos[Dead,Alive].X, ~pos[Dead,Alive].Y
*/


struct EyeStalk : public XY<EyeMuscle> {
  EyeState es = ~0; //for behavior stuff, we will want to do things like "if not dead then"
  EyeStalk (short xw, short yw): XY<EyeMuscle>(xw, yw) {}

  /** set to a pair of values */
  using XY<EyeMuscle>:: operator =;
  /** position when related stalk is dead.*/
  Gaze dead = {AnalogValue::Min, AnalogValue::Min}; //will tweak for each stalk, for effect
  /** position when related stalk is under attack.*/
  Gaze alert = {AnalogValue::Max, AnalogValue::Max}; //will tweak for each stalk, for effect

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

  Gaze pos() {
    return Gaze(X.pos, Y.pos);
  }

  void resend() {
    *this = pos();
  }

  // range.x.range.y.position.dead.position.alert
  //  	400,200x	400,200y	1,2D	20000,20001A"  //using ls digit as tracer for program debug.
  size_t printTo(Print& p) const {
    return
      p.print('\t') +
      p.print(X.range) + p.print('x')
      + p.print('\t') +
      p.print(Y.range) + p.print('y')
      + p.print('\t') +
      p.print(dead) + p.print('D')
      + p.print('\t') +
      p.print(alert) + p.print('A')
      ;
  }

};


//records recent joystick value
Gaze joy(0, ~0);//weird init so we can detect 'never init'

void showJoy() {
  Console(FF("Joy x: "), joy);
}


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
// the big eye is coded as eyestalk[0] so that we can share tuning and testing code.

//pairing jaw and eyebrow as eyestalk[7] so that we can borrow eyestalk tuning and testing code
EyeMuscle &brow(eyestalk[7].Y);
EyeMuscle &jaw(eyestalk[7].X);

//iterator over actual stalks:
#define Allstalk(ei) for (unsigned ei = 6; ei-- > 1;)

//show all pwm outputs
void showRaw() {
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {
    Console(FF("Dev["), ei, "] ", eyestalk[ei].X.adc, ",", eyestalk[ei].Y.adc);
  }
}

//UI state
struct UI {
  bool amMonster = false; //detects which end of link
  bool updateEyes = true; //enable joystick actions
  bool tuning = false; //allow configuration adjustments
  //channel being tuned
  short tunee = 0;
  // which muscle when tuning half of a pair
  bool wm = 0;

  //full keystroke echo, added for figuring out non-ascii keys.
  bool rawecho = false;


  /** @returns whether 'all' is the selected eyestalk */
  bool doAll() const {
    return tunee == 9;
  }

  //muscle of interest
  Muscle &moi() const {
    if (tunee < countof(eyestalk)) {
      Muscle &muscle( wm ? eyestalk[tunee].X : eyestalk[tunee].Y);
      return muscle;
    } else {//anything other than random memory
      return ( wm ? jaw : brow);
    }
  }

  void update(bool on) {
    if (changed(updateEyes, on)) {
      if (on) {
        Console(FF("Enabling joystick"));
      } else {
        Console(FF("Freezing position"));
      }
    }
  }

} ui;

//all eyestalks in unison
void joy2all(Gaze xy) {
  Allstalk(ei) {//exclude big eyeball, brow and jaw
    eyestalk[ei] = xy;
  }
}

void joy2eye(Gaze xy) {
  if (ui.doAll()) { //do all
    joy2all(xy);
  } else {
    eyestalk[ui.tunee] = xy;
  }
}


void setRange(unsigned topper, unsigned lower) {
  if (ui.tuning) {
    Muscle &muscle( ui.moi());
    muscle.range = LinearMap(topper, lower);
    Console(FF("Range: "), muscle.range);
  }
}


void tweak(bool plusone, unsigned value) {
  if (ui.tuning) {
    ui.wm = plusone;
    Muscle &muscle( ui.moi());
    muscle.test(value);
    Console(FF("pwm["), muscle.hw.which, "]=", muscle.adc, " from:", value);
  }
}

void doarrow(bool plusone, bool upit) {
  if (ui.tuning) {
    ui.wm = plusone;
    Muscle &muscle( ui.moi());
    tweak(plusone, muscle.adc + upit ? 10 : -10);
  }
}

void knob2range(bool top, unsigned value) {
  if (ui.tuning) {
    Muscle &muscle( ui.moi());
    if (top) {
      muscle.range.top = value;
    } else {
      muscle.range.bottom = value;
    }
  }
}

void allbe(EyeState es) {
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {//all sets
    eyestalk[ei].be(es);
  }
}


void be(EyeState es) {
  if (ui.doAll()) {
    allbe(es);
  } else {
    eyestalk[ui.tunee].be(es);
  }
}

void center() {
  Gaze centerpoint(AnalogValue::Half, AnalogValue::Half);
  if (ui.doAll()) {
    Allstalk(ei) {
      eyestalk[ei] = centerpoint;
    }
  } else {
    eyestalk[ui.tunee] = centerpoint;
  }
}

void resendAll() {//naming this resend triggers an Arduino error, gets confused as to whether this is a simple function or a member of eyestalk. Weird.
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {//all sets
    eyestalk[ei].resend();
  }
}

void livebe(EyeState es) {
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {//all sets
    EyeStalk &eye(eyestalk[ei]);
    if (eye.es != EyeState::Dead) {
      eye.be(es);
    }
  }
}

/** we always go to the position to record, then record it. So we can reuse the position request variable as the value to save*/
void record(EyeState es) {
  if (ui.tuning) {
    switch (es) {
      case Dead:
        eyestalk[ui.tunee].dead = joy;
        break;
      case Alert:
        eyestalk[ui.tunee].alert = joy;
      default:
        //Console(FF("\nBad eyestate in record",es);
        break;
    }
  }
}


////////////////////////////////////////////////////////////////
/*
  wiggling:
  use common oscillator for all muscles,
  use computed amplitude for y vs x and
  use different phases for each eye, roughly 1/n of cycle.
  run logic cycle at 1/6 wiggle cycle, count to 6 and change the direction of that stalk on its cycle
*/
class Wiggler {
  public:
    const byte numstalks = 6; //might drop to 5

    MonoStable timer;//let setup start us
    byte stalker = numstalks;
    unsigned wigamp;

    void operator()() {
      if (timer.perCycle()) {
        if (!--stalker) {
          stalker = numstalks;
          if (eyestalk[6].es == EyeState::Seeking) {
            showRaw();
          }
        }
        EyeStalk &eye(eyestalk[stalker]);
        if (eye.es == EyeState::Seeking) {
          eye.X = (~eye.X.pos > AnalogValue::Half) ? AnalogValue::Min : AnalogValue::Max;
          eye.Y = AnalogValue::Half + random(wigamp);
          Console(FF("wig:"), stalker, "\t", eye);
        }
      }
    }

    void rate(unsigned ms) {
      timer.set(ms / numstalks);
    }

    MilliTick hparam()const {
      return MilliTick(timer);
    }

    void yamp(unsigned amp) {
      wigamp = amp;
    }
} wiggler;


////////////////////////////////////////////////////////////////
//
// Command Interpreter
//
////////////////////////////////////////////////////////////////
#include "unsignedrecognizer.h"
UnsignedRecognizer param;
//for 2 parameter commands:
unsigned pushed = 0;

#include "linearrecognizer.h"
LinearRecognizer ansicoder[2] = {"\e[", "\eO"};

void setJoy() {
  joy.X = take(pushed);
  joy.Y = param;
  joy2eye(joy);
}

/** either record the given position as a the given state, or go into that state */
void doSetpoint(boolean set, EyeState es ) {
  if (set && ui.tuning) {
    if ( ~param && pushed != 0) {
      setJoy();//goes to entered position
    }
    record(es);
    Console(FF("Recorded "), ui.tunee, "\tsetpoint:", es, "\t:", joy);
  } else {//goto dead position
    if (~param) {
      ui.tunee = param;
    }
  }
  be(es);
  Console(FF("Stalk "), ui.tunee, " to setpoint:", es);
}

unsigned showConfig(Print &p) {
  unsigned size = 0;
  for (unsigned ei = countof(eyestalk); ei-- > 0;) {//all sets
    //	"\n	1w"  plus string from eyestalk
    size +=
      p.print("\n\t") + p.print(ei) + p.print('w') + p.print(eyestalk[ei]);
  }
  //    "\n	500h	24000H"  //wiggler config rate then yamp
  size +=
    p.print("\n\t") + p.print(wiggler.hparam()) + p.print("h\t") + p.print(wiggler.wigamp) + p.print('H') ;
}

unsigned showConfig(Print &&p) {
  showConfig(p);
}

/** made this key switch a function so that we can return when we have consumed the key versus some tortured 'exit if' */
void doKey(byte key) {
  if (key == 0) { //ignore nulls, might be used for line pacing.
    return;
  }
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
        switch (unsigned(param)) {
          case 3://del
          case 5://page up
          case 6://page dn
            break;
        }
        break;
      default: //code not understood
        //don't treat unknown code as a raw one. esc [ x is not to be treated as just an x with the exception of esc itself
        if (!ansicoder[0](key)) {
          Console(FF("Unknown ansi [ code:"), char(key), " param:", param);
        }
        break;
    }
    return;
  }

  if (~ansicoder[1]) {//test and clear
    switch (key) {//ansi code
      case 'H':
        Console(FF("Well Hello!"));
        break;
      case 'F':
        Console(FF("Goodbye!"));
        break;
      case 'P'://F1..F4
      case 'Q':
      case 'R':
      case 'S':
        Console(FF("Romans:"), key - 'P');
        break;
      //      case 'D':
      //        break;

      default: //code not understood
        //don't treat unknown code as a raw one. esc [ x is not to be treated as just an x with the exception of esc itself
        if (!ansicoder[1](key)) {
          Console(FF("Unknown ansi O code:"), char(key), " param:", param);
        }
        break;
    }
    return;
  }

  if (ansicoder[1](key) | ansicoder[0](key)) {//# | not ||, must check all, return if any.
    return; //part of a prefix
  }

  switch (key) {//used: aAbcdDefFhHiIjlopstwxyzZ  @ *!,.
    case '\t'://ignore tabs, makes param files easier to read.
      break;
    case ','://push a parameter for 2 parameter commands.
      pushed = param;
      break;
    case '.'://simulate joystick value
      setJoy();
      break;
    case 'n'://by sending a value instead of a boolean the remote can impement 'half-open' and the like.
      jaw = AnalogValue(param);
      break;
    case 'm':
      brow = AnalogValue(param);
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

    case 'c': //center all entities but don't change state
      center();
      break;

    case 'r'://resend,recover from pwm board power glitch.
      resendAll();
      break;

    case 'F'://set pwm frequency parameter, 122 for 50Hz.
      Console(FF("Prescale set to: "), param);
      pwm.setPrescale(param, true);
    //#join
    case 'f':
      Console(FF("prescale: "), pwm.getPrescale());
      break;

    case 27://escape is easier to hit then shiftZ
    case 'Z': //reset scene
      allbe(EyeState::Seeking);
      ui.tunee = 0;
      ui.tuning = false;
      break;
    case 'z': //resume wiggling
      livebe(EyeState::Seeking);
      ui.tunee = 0; //in case we make alert connect the joystick to the one stalk.
      ui.tuning = false;
      break;

    case 'w': //select eye of interest
      ui.tunee = param;
      Console(FF("Eye "), ui.tunee);
      break;

    case ':':
      ui.tuning = true;
      break;

    case 'e'://set one muscle to absolute position
      tweak(0, param);
      break;
    case 's'://set other muscle to absolute position
      tweak(1, param);
      break;

    case 'b'://set lower end of range for most recently touched muscle
      knob2range(0, param);
      break;
    case 't'://set higher end of range for most recently touched muscle
      knob2range(1, param);
      break;
    case 'x': //pick axis, if values present then set its range.
    case 'y':
      ui.wm = key & 1;
      if (pushed) { //zero is not a reasonable range max value so we can key off this to idiot check entry.
        setRange( take(pushed), param);
      }
      break;
    case 'H'://wiggle relative amplitude
      if (~param) {
        wiggler.yamp(param);
        Console(FF("yamp:"), wiggler.wigamp);
      }
      break;
    case 'h'://wiggle rate
      if (~param) {
        wiggler.rate(param);
        Console(FF("wig:"), MilliTick(wiggler.timer));
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
      Console(FF("raw echo:"), ui.rawecho);
      break;
    case 'i':
      showConfig(Console.uart.raw);
      break;
    case 'I':
      switch (param) {
        case 0://load from eeprom
          Init.load();
          break;
        case 1://load from program
          Init.restore(false);
          break;
        case 2://generate
          showConfig(Init.saver());
          break;
        case 42://load from program and write to eeprom
          Init.restore(true);
          break;
      }
      break;
    case '@'://set each servo output to a value computed from its channel #
      pwm.idChannels(0, 15);
      Console(FF("pwm IDs sent"));
      break;
    default:
      Console(FF("Unknown command:"), key, ' ', int(key));
      break;
    case '\n'://clear display via shoving some crlf's at it.
    case '\r':
      Console(FF("\n\n\n"));
      break;
  }
}


////////////////////////////////////////////////
//
//  SETUP          AND          LOOP          //
//
////////////////////////////////////////////////
//whether board was detected at startup. Should periodically check again so that wiggling a cable cn fix it.
bool pwmPresent = false;
//cache buttons, read only once per loop in case we add debouncing to read routine.
bool browUp;
bool jawOpen;

/** @returns whether pwm just got noticed */
bool pwmOnline() {
  if (!pwmPresent) {
    if (changed(pwmPresent, pwm.begin(4, 50))) { //4:totempole drive.
      return true;
    }
  }
  return false;
}


void setup() {
  T6 = 1;
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.

  Wire.begin(); //not trusting pwm begin to remember to do this.
  Wire.setClock(400000);//pca9685 device can go 1MHz, but 32U4 cannot.
  pwmOnline();//for powerup message

  unsigned cfgsize = Init.load();
  ui.amMonster = BeMonster;
  if (ui.amMonster) {//conditional on which end of link for debug.
    Console(FF("Init block is "), cfgsize, " bytes");//process config from eeprom
    Console(pwmPresent ? FF("Behold") : FF("Where is"), FF(" the Beholder (bin: " REVISIONMARKER ")\n\n\n")); //todo: git hash insertion.
    doKey('Z');//be wiggling upon a power upset.
  } else {
    Remote(FF("Remote Control of"), FF(" the Beholder (bin: " REVISIONMARKER ")\n\n\n")); //todo: git hash insertion.
  }
  T6 = 0;
}


void loopMonster() {
  if (MilliTicked) { //this is true once per millisecond.
    wiggler();
    joy = joydev;//normalizes scale.
    if (ui.updateEyes) {
      joy2eye(joy);
    }
    brow = (browup ? AnalogValue::Max : AnalogValue::Min);
    jaw = (jawopen ? AnalogValue::Max : AnalogValue::Min);

    if (pwmOnline()) {//then just discovered pwm chip after an absence.
      //send desired state to all channels.
      resendAll();
    }
  }

  byte key = Console.getKey();
  if (key > 0) {
    if (ui.rawecho) {
      Console("\tKey: ", key, ' ', char(key));
    }
    doKey(key) ;
  }
}



void loopController() {
  if (MilliTicked) { //this is true once per millisecond.
    joy = joydev;
    if (ui.updateEyes) {
      Remote(joy, '.');
    }
    if (changed(browUp, browup)) {
      Remote((browUp ? AnalogValue::Max : AnalogValue::Min), 'm');
    }
    if (changed(jawOpen , jawopen )) {
      Remote((jawOpen  ? AnalogValue::Max : AnalogValue::Min), 'n');
    }
  }

  int key = Local.getKey();
  if (key > 0) {
    if (ui.rawecho) {
      Local("\tKey: ", key, ' ', char(key));
    }
    //    doKey(key);
    Remote.conn.write(key);//relay to monster input.
  }

  int trace = Remote.getKey();
  if (trace > 0) { //relay monster output
    Local.conn.write(trace);
  }
}

void loop() {
  if (changed(ui.amMonster, BeMonster)) {//then jumper is glitchy. We want to know about that.
    if (ui.amMonster) {
      Console(FF("!# I am the BEHOLDER!"));
    } else {
      Local(FF("!# I am the CONTROLLER!"));
    }
  }
  if (ui.amMonster) {
    loopMonster();
  } else {
    loopController();
  }
}
