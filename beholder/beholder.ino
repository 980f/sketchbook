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
#define REVISIONMARKER "2019feb21-21:14"

///////////////////////////////////////////////////////////////
//this chunk takes advantage of the c compiler concatenating adjacent quote delimited strings into one string.
const PROGMEM char initblock[] =
  //channel.w.range.x.range.y.position.dead.position.alert
  ":" //enable tuning commands
  "\n	1w	400,200x	410,200y	11,2D	20000,20001A"  //using ls digit as tracer for program debug.
  "\n	2w	400,200x	420,200y	22,3D	20000,20002A"
  "\n	3w	400,200x	430,200y	33,2D	20000,20003A"
  "\n	4w	400,200x	440,200y	44,5D	20000,20004A"
  "\n	5w	400,200x	450,200y	55,2D	20000,20005A"
  "\n	6w	400,200x	460,200y	66,9D	20000,20006A"
  "\n	0w	400,200x	400,200y	99,0D	20000,20000A" //big eye
  "\n	7w	400,200x	470,200y	77,6D	20000,20007A" //jawbrow
  "\n	500h	24000H"  //wiggler config rate then yamp
  ;
#include "initer.h"
void doKey(byte key);
Initer Init(RomAddr(initblock), doKey);

/////////////////////////////////////////////////////////////

#include "bitbanger.h" //early to replace Arduino's macros with real code
#include "minimath.h"  //for template max/min instead of macro
#include "cheaptricks.h" //for changed()

#include "pinclass.h"
//#include "digitalpin.h"
#include "millievent.h"

/** merge usb serial and serial1 streams.*/
#include "twinconsole.h"
TwinConsole Console;

/** streams kept separate for remote controller, where we do cross the streams. */
#include "easyconsole.h"
EasyConsole<decltype(Serial)> Local(Serial);//usb
EasyConsole<decltype(Serial1)> Remote(Serial1);//hw serial

#include "scani2c.h" //diagnostic scan for devices. the pc9685 shows up as 64 and 112 on power up, I make the 112 go away in setup() which conveniently allows us to distinguish reset from power cycle.
#include "pca9685.h" //the 16 channel servo  controller
PCA9685 pwm; //default address and bus
PCA9685 shadow(0x41);//can conditionalize this if we figure out how to not burn up boards.

////////////////////////////////////////////////////////
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
//will use a command instead, the pin is not stable without a jumper.
//const InputPin<16, HIGH> BeMonster;

//digital inputs to control eyebrow and jaw:
const InputPin<14, LOW> jawopen;
const InputPin<15, LOW> browup;

//A0,A1,A2,A3 used for joystick and pots.
////////////////////////////////////////////////////////

#include "analog.h" //deals with the difference in number of bits of analog info in, out, and simulated  
#include "linearmap.h" //simpler scaling component than Arduino map(), also syntactically cuter.

#include "xypair.h"  //the eyestalks are 2 dimensional
/** A 2D coordinate value. */
using Gaze = XY<AnalogValue>;

//joystick device
struct Joystick {
  const XY<const AnalogInput> dev;
  //records recent joystick value
  Gaze pos;

  Joystick(byte xpin, byte ypin): dev(xpin, ypin), pos(0, 0) {}

  operator Gaze() const {
    return pos;
  }

  /** @returns whether pos has changed since last time this was called. */
  bool operator()() {
    return pos.assignFrom(dev); //normalizes scale.
  }

  void show() {
    Console(FF("Joy x: "), pos);
  }

};
Joystick joy(A2, A3);


//pair of pots, a joy stick without a spring ;)
Joystick pots(A0, A1);

//button is either local or remote.
struct RemotableButton {
  bool active;
  bool operator ()(bool newvalue) {
    return changed(active, newvalue);
  }
  operator bool()const {
    return active;
  }
};

RemotableButton jawOpener;
RemotableButton browRaiser;

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

  /** easy creator, but compiler wants to create copies for assignment and gets confused with operator = without the 'explicit'
      with multiple controller pwm becomes pwm[] and we split the channel to pick one here. The rest of the program won't know which chip is in use.
  */
  explicit Muscle(uint8_t which): Muscle(pwm, which) {}

  /** goto some position, as fraction of configured range. */
  void operator =(AnalogValue pos) {
    if (changed(adc, range(this->pos = pos))) {
      //      Console(FF("Muscle "),hw.which," to ",pos);
      hw = adc;
    }
  }

  /** move to one extreme or the other */
  void flail(bool max) {
    operator = ( AnalogValue (max ? AnalogValue::Max : AnalogValue::Min, 15));
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

  /** emit present location */
  size_t printTo(Print& p) const {
    return p.print(~pos);
  }

};

/** each stalk is in one of these states */
enum EyeState : uint8_t { //specing small type in case we shove it directly into eeprom
  //#order matters as it is used as an array index, add new stuff to the end.
  Dead,   // as limp appearing as possible
  Alert,  // looking at *you*
  Seeking, // wiggling aimlessly

};


/** actuator to use */
using EyeMuscle = Muscle;

struct EyeStalk : public XY<EyeMuscle> {
  //for behavior stuff, we will want to do things like "if not dead then"
  EyeState es = Dead; //init to dead so beast is quiet until we are sure it is configured.
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

  /** @returns coordinat eye is presently set to.*/
  Gaze pos(EyeState es = Seeking) {
    switch (es) {
      case Dead:
        return dead;
        break;
      case Alert:
        return alert;
        break;
      default:
        return Gaze(X.pos, Y.pos);
    }
  }

  /** calling this 'refresh' pissed off Arduino, the autoheader logic failed. */
  void resend() {
    *this = pos();
  }

  /** emit configuration string */
  size_t printTo(Print& p) const {
    return
      p.print('\t')
      + p.print(X.range) + p.print('x')
      + p.print('\t')
      + p.print(Y.range) + p.print('y')
      + p.print('\t')
      + p.print(dead) + p.print('D')
      + p.print('\t')
      + p.print(alert) + p.print('A')
      ;
  }

};


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
#define Allstalk(ei) for (unsigned ei = 6; ei>0;--ei)
#define AllGizmos(ei) for (unsigned ei = countof(eyestalk); ei-- > 0;)

//show all pwm outputs
void showRaw() {
  AllGizmos(ei) {
    Console(FF("Dev["), ei, "] ", eyestalk[ei].X.adc, ",", eyestalk[ei].Y.adc);
  }
}

//UI state
struct UI {
  bool amMonster = true;  //indicates which end of link, default to monster as that is the one that matters.
  bool updateEyes = true; //enable joystick actions
  bool tuning = false;    //allow configuration adjustments
  bool wm = 0;            //which muscle when tuning half of a pair
  bool rawecho = false;   //full keystroke echo, added for figuring out non-ascii keys.
  bool all = false;
  byte tunee = 0;        //eyestalk  being tuned

  /**muscle of interest */
  Muscle &moi() const {
    if (tunee < countof(eyestalk)) {
      Muscle &muscle( wm ? eyestalk[tunee].X : eyestalk[tunee].Y);
      return muscle;
    } else {//anything other than random memory
      return ( wm ? jaw : brow);
    }
  }

  void selectStalk(unsigned which) {
    all = (which == ~0U);
    tunee = which % countof(eyestalk);
  }

  EyeStalk &stalk() {
    //    Console(FF("Stalk:"),tunee);
    return eyestalk[tunee];
  }

  /** enable or disable running stalks from joystick */
  void update(bool on) {
    if (changed(updateEyes, on)) {
      if (on) {
        Console(FF("Enabling joystick"));
      } else {
        Console(FF("Freezing position"));
      }
    }
  }

  /** @param on means this unit is remote controlling the monster, else it is the monster*/
  void unleash(bool off) {
    if (changed(amMonster, off)) {
      if (amMonster) {
        Console(FF("!# I am the BEHOLDER!"));
      } else {
        Local(FF("!# I am the CONTROLLER!"));
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
  if (ui.all) {
    joy2all(xy);
  } else {
    ui.stalk() = xy;
  }
}

/** servo values (~300) not positions */
void setRange(unsigned topper, unsigned lower) {
  if (ui.tuning) {
    Muscle &muscle( ui.moi());
    muscle.range = LinearMap(topper, lower);
    Console(FF("Range: "), muscle.range);
  }
}

/** set a muscle to a servo value */
void tweak(bool plusone, unsigned value) {
  if (ui.tuning) {
    ui.wm = plusone;
    Muscle &muscle( ui.moi());
    muscle.test(value);
    Console(FF("pwm["), muscle.hw.which, "]=", muscle.adc, " from:", value);
  }
}

/** bump  a muscle up or down in servo units */
void doarrow(bool plusone, bool upit) {
  if (ui.tuning) {
    ui.wm = plusone;
    Muscle &muscle( ui.moi());
    tweak(plusone, muscle.adc + upit ? 10 : -10);
  }
}

/** set one end of a muscle's range */
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

/** set state of all mechanisms */
void allbe(EyeState es) {
  AllGizmos(ei) {//all sets
    eyestalk[ei].be(es);
  }
}

/** set stat of one mechanism, perhaps all. */
void be(EyeState es) {
  if (ui.all) {
    allbe(es);
  } else {
    ui.stalk().be(es);
  }
}

/** move all actuators to center of their allowed range */
void center() {
  Gaze centerpoint(AnalogValue::Half, AnalogValue::Half);
  if (ui.all) {
    Allstalk(ei) {
      eyestalk[ei] = centerpoint;
    }
  } else {
    ui.stalk() = centerpoint;
  }
}

/** refresh hardware registers with values they shouldhave in them, useful if pwm chip glitches. */
void resendAll() {//naming this resend triggers an Arduino error, gets confused as to whether this is a simple function or a member of eyestalk. Weird.
  AllGizmos(ei) {
    eyestalk[ei].resend();
  }
}

/** changes state of mechanisms which are dead */
void livebe(EyeState es) {
  AllGizmos(ei) {
    EyeStalk &eye(eyestalk[ei]);
    if (eye.es != EyeState::Dead) {
      eye.be(es);
    }
  }
}

/** record a direction to gaze in. */
void record(EyeState es, Gaze gaze) {
  if (ui.tuning) {
    switch (es) {
      case Dead:
        ui.stalk().dead = gaze;
        break;
      case Alert:
        ui.stalk().alert = gaze;
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
            //            showRaw();
          }
        }
        EyeStalk &eye(eyestalk[stalker]);
        if (eye.es == EyeState::Seeking) {
          eye.X = (~eye.X.pos >= AnalogValue::Half) ? AnalogValue::Min : AnalogValue::Max;
          eye.Y = AnalogValue::Half + random(wigamp);
          //          Console(FF("wig:"), stalker, "\t", eye);
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
#include "unsignedrecognizer.h"  //recognize numbers but doesn't deal with +/-
UnsignedRecognizer numberparser;
//for 2 parameter commands, gets value from param.
unsigned arg = 0;
unsigned pushed = 0;

#include "linearrecognizer.h" //simple state machine for recognizing fixed sequences of bytes.
LinearRecognizer ansicoder[2] = {"\e[", "\eO"}; //empirically discoverd on piTop.

/** read joystick and send to eye */
void setJoy(const Gaze &gaze) {
  joy.pos = gaze;
  joy2eye(joy);
}

/** either record the given position as a the given state, or go into that state */
void doSetpoint(boolean set, EyeState es, const Gaze &gaze) {
  if (set && ui.tuning) {
    if (!gaze.both(0)) {
      record(es, gaze);
    } else {
      Console(FF("Recording present position"));
      record(es, joy);
    }
    Console(FF("Recorded "), ui.tunee, "\tsetpoint:", es, "\tas:", ui.stalk().pos(es));
  } else {//goto dead position
    if (arg) {//was too easy to hit d without a selection and kill the big eye. If we aren't worred then we can drop this 'if'. 
      ui.selectStalk(arg);
    }
  }
  be(es);
  Console(FF("Stalk "), ui.tunee, " to setpoint:", es, '@', ui.stalk().pos());
}

/** emit present configuration to given printer, the eeprom is a printer so this is how configuration is saved. */
unsigned showConfig(Print &p) {
  unsigned size = 0;
  AllGizmos(ei) { //all sets
    //	"\n	1w"  plus string from eyestalk
    size +=
      p.print("\n\t") + p.print(ei) + p.print('w') + p.print(eyestalk[ei]);
  }
  //    "\n	500h	24000H"  //wiggler config rate then yamp
  size +=
    p.print("\n\t") + p.print(wiggler.hparam()) + p.print("h\t") + p.print(wiggler.wigamp) + p.print('H') ;
  return size;
}

/** compiler needed some help, @see other showConfig()*/
unsigned showConfig(Print &&p) {
  return showConfig(p);
}

/** command processor */
void doKey(byte key) {
  if (key == 0) { //ignore nulls, might be used for line pacing.
    return;
  }
  //test digits before ansi so that we can have a numerical parameter for those.
  if (numberparser(key)) { //part of a number, do no more
    return;
  }
  arg = numberparser; //read and clear, regardless of whether used.
  Gaze gaze(take(pushed), (arg));

  //see if part of an escape sequence.
  switch (ansicoder[0] <= key) {
    case 1:
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
          switch (arg) {
            case 3://del
            case 5://page up
            case 6://page dn
              break;
          }
          break;
        default: //code not understood
          //don't treat unknown code as a raw one. esc [ x is not to be treated as just an x with the exception of esc itself
          Console(FF("Unknown ansi [ code:"), char(key), " arg:", arg);
          break;
      }//switch on code
    //#join
    case 0://is some part of escape sequence. bool(seq) true if perfect end.
      return;
    default:
      break;
  }

  switch (ansicoder[1] <= key) {
    case 1:
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
          Console(FF("Romans: "), key - 'P');
          break;
        //      case 'D':
        //        break;

        default: //code not understood
          //don't treat unknown code as a raw one. esc [ x is not to be treated as just an x with the exception of esc itself
          Console(FF("Unknown ansi O code: "), char(key), " arg: ", arg);
          break;
      }
    case 0:
      return;
    default:
      break;
  }

  switch (key) {//used: aAbcdDefFhHiIjlMmNnoprstwxyzZ  :@ *!,.   tab cr newline
    case '\t'://ignore tabs, makes param files easier to read.
      break;
    case ','://push a parameter for 2 parameter commands.
      pushed = arg;
      //      Console("\tPushed:", pushed, " 0x", HEXLY(pushed));
      break;

    case '.'://simulate joystick value
      setJoy(gaze);
      break;
    case 'n'://by sending a value instead of a boolean the remote can implement 'half-open' and the like.
      jaw = AnalogValue(arg);
      break;
    case 'N':
      jaw.flail(arg);
      break;
    case 'm':
      brow = AnalogValue(arg);
      break;
    case 'M':
      brow.flail(arg);
      break;

    case 'D': //record dead position
      doSetpoint(true, EyeState::Dead, gaze);
      break;
    case 'd': //goto dead position
      doSetpoint(false, EyeState::Dead, gaze);
      break;

    case 'A': //record alert position
      doSetpoint(true, EyeState::Alert, gaze);
      break;
    case 'a': //goto alert position
      doSetpoint(false, EyeState::Alert, gaze);
      break;

    case 'c': //center all entities but don't change state
      center();
      break;

    case 'r'://resend,recover from pwm board power glitch.
      resendAll();
      break;

    case 'F'://set pwm frequency parameter, 122 for 50Hz.
      Console(FF("Setting prescale to: "), arg);
      pwm.setPrescale(arg, true);
    //#join
    case 'f':
      Console(FF("prescale: "), pwm.getPrescale());
      break;

    case 27://escape is easier to hit then shiftZ
    case 'Z': //reset scene
      allbe(EyeState::Seeking);
      ui.selectStalk(0);
      ui.tuning = false;
      break;
    case 'z': //resume wiggling
      livebe(EyeState::Seeking);
      ui.selectStalk(0);//in case we make alert connect the joystick to the one stalk.
      ui.tuning = false;
      break;

    case 'w': //select eye of interest
      ui.selectStalk(arg);
      Console(FF("Eye "), ui.tunee);
      break;

    case ':'://UI state management
      switch (arg) {
        case 0://0 or none means "you are the monster"
          ui.unleash(true);
          break;
        case 1: //be remote control
          ui.unleash(false);
          break;
        case 2: //enable extended function set, especially configuration editing
          ui.tuning = true;
          Console(FF("Tuning enabled, Z to disable"));
          break;
        default:
          Console(FF("Bad ':' command "), arg);
          break;
      }

      break;

    case 'e'://set one muscle to absolute position
      tweak(0, arg);
      break;
    case 's'://set other muscle to absolute position
      tweak(1, arg);
      break;

    case 'b'://set lower end of range for most recently touched muscle
      knob2range(0, arg);
      break;
    case 't'://set higher end of range for most recently touched muscle
      knob2range(1, arg);
      break;
    case 'x': //pick axis, if values present then set its range.
    case 'y':
      ui.wm = ~key & 1;
      if (!gaze.both(0)) {
        setRange( ~gaze.X, ~gaze.Y);
      }
      break;
    case 'H'://wiggle relative amplitude
      if (arg) {
        wiggler.yamp(arg);
        Console(FF("yamp: "), wiggler.wigamp);
      }
      break;
    case 'h'://wiggle rate
      if (arg) {
        wiggler.rate(arg);
        Console(FF("wig: "), MilliTick(wiggler.timer));
      }
      break;

    case 'p':
      joy.show();
      break;
    case 'j':
      showRaw();
      break;
    case ' ':
      joy.show();
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
      ui.rawecho = arg;
      Console(FF("raw echo: "), ui.rawecho);
      break;
    case 'i'://working ->monitor
      Console("#!{");//trigger for remote recognition.
      showConfig(Console.uart.raw);
      showConfig(Console.usb.raw);
      Console("#!}");//
      break;
    case 'I'://configuration management
      switch (arg) {
        case 0://eeprom-> working
          Init.load();
          break;
        case 1://rom -> working
          Init.restore(false);
          break;
        case 2://eeprom ->monitor
          for (EEPointer eep = Init.saver(); eep;) { //don't run off the end of existence
            byte c = *eep++;
            Console.write(c);
          }
          break;
        case 3://rom->monitor
          for (RomPointer rp(Init.initdata); ;) {//abusing for loop to get brackets.
            byte c = *rp++;
            if (!c) {
              break;
            }
            Console.write(c);
          } break;
        case 4://working ->eeprom
          EEPROM[showConfig(Init.saver())] = 0;
          break;
        case 42://load from program and write to eeprom
          Init.restore(true);
          break;
        case 86: //erase eeprom
          for (EEPointer eep = Init.saver(); eep.hasNext();) { //don't run off the end of existence
            eep.next() = 0;
          }
          Console(FF("Config erased"));
          break;
      }
      break;
    case '@'://set each servo output to a value computed from its channel #
      pwm.idChannels(0, 15);
      Console(FF("pwm IDs sent"));
      break;
    default:
      Console(FF("Unknown command: "), char(key), ' ', int(key));
      break;
    case '\n'://clear display via shoving some crlf's at it.
    case '\r':
      Console(FF("\n\n\n"));
      break;
  }
}


/** the hotspare logic tries to record configuration of the remote system.*/
struct Hotspare {
  LinearRecognizer marker;//stuff between #!{  and #!} get saved to eeprom
  bool recording = false;
  EEPrinter writer;
  Hotspare (): marker("#!") {}
  /** extract configuration block report from trace and apply it locally */
  void operator()(byte trace) {
    switch (marker <= trace) {
      case 1://char after marker
        switch (trace) {
          case '{':
            //start eeprom write sequencer
            writer = Init.saver();
            recording = true;
            break;
          case '}':
            //stop eeprom writing
            recording = false;
            break;
        }
        break;
      case 0://in sequence, absorb tokens, if they aren't the end then something has gone horribly,horribly wrong.
        break;
      default:
        if (recording) {
          *writer++ = trace;
        }
        break;
    }
  }
} hs;

////////////////////////////////////////////////
//
//  SETUP          AND          LOOP          //
//
////////////////////////////////////////////////
//whether board was detected at startup. Should periodically check again so that wiggling a cable cn fix it.
bool pwmPresent = false;

/** @returns whether pwm just got noticed */
bool pwmOnline() {
  pwm.shadow = &shadow;//must do this before first pwm.begin or carefully also invoke 'shadow.begin(...)'. We would do the latter if inverting outputs for differential drive.
  if (!pwmPresent) {
    if (changed(pwmPresent, pwm.begin(4, 50))) { //4:totempole drive.
      return true;
    }
  }
  return false;
}

#define RevisionMessage F(" the Beholder (bin: " REVISIONMARKER ")\n\n\n")

void setup() {
  T6 = 1;
  pinMode(0, INPUT_PULLUP); //RX is picking up TX on empty cable.

  Console.begin();

  Wire.begin(); //not trusting pwm begin to remember to do this.
  Wire.setClock(400000);//pca9685 device can go 1MHz, but 32U4 cannot.
  pwmOnline();//for powerup message

  unsigned cfgsize = Init.load();

  ui.updateEyes = false;//joystick spews if not attached.

  Console(FF("Init block is "), cfgsize, " bytes");//process config from eeprom
  Console(pwmPresent ? FF("Behold") : FF("Where is"), RevisionMessage);
  doKey('Z');//be wiggling upon a power upset.
  T6 = 0;
}


void loopMonster() {
  if (MilliTicked) { //this is true once per millisecond.
    if (ui.updateEyes) { //may need its own kill.
      wiggler();
    }

    if (joy() && ui.updateEyes) {
      joy2eye(joy);
    }
    if (browRaiser(browup)) {
      brow.flail(browRaiser);
    }
    if (jawOpener(jawopen )) {
      jaw.flail(jawOpener);
    }
    if (pwmOnline()) {//then just discovered pwm chip after an absence.
      //send desired state to all channels.
      resendAll();
    }
  }

  byte key = Console.getKey();
  if (key > 0) {
    if (ui.rawecho) {
      Console("\tKey: ", key, ' ', char(key), '\t', numberparser.accumulator);
    }
    doKey(key) ;
  }
}


void loopController() {
  if (MilliTicked) { //this is true once per millisecond.
    if (joy() && ui.updateEyes) {
      Remote(joy, '.');
    }
    if (browRaiser(browup)) {
      Remote(browRaiser, 'M');
    }
    if (jawOpener(jawopen )) {
      Remote(jawOpener, 'N');
    }
  }

  int key = Local.getKey();
  if (key > 0) {
    if (ui.rawecho) {
      Local("\tKey: ", key, ' ', char(key), '\t', numberparser.accumulator);
    }
    //we need a single key to get us back into 'not controller' mode.
    if(key==22){//^V
    	ui.unleash(true);//start processing commands locally, useful in case you accidentally configure to be remote, and to verify configuration capture.
    } else {
    	Remote.conn.write(key);//relay to monster input.
    }
  }

  int trace = Remote.getKey();
  if (trace > 0) { //relay monster output
    Local.conn.write(trace);//to human via USB serial adapter
    hs(trace);//records configuration listing into local eeprom. 
  }
}

void loop() {
  if (ui.amMonster) {
    loopMonster();
  } else {
    loopController();
  }
}
