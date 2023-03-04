/**
  control timing of drawer kicker for Orkules puzzle.

  1) activate drawer latch (HW note: is not polarity sensitive, can use half-H driver)
  2) wait enough time for it to retract
  3) activate kicker
  4) wait long enough for it to fully extend. A little extra time is not harmful but don't leave it on for more than a second.
  5) reverse kicker direction. Break before make etc are not required, the possible intermediate states are just fine with this driver (H-bridge with free wheel and brake as the transitional states)
  6) wait long enough for it to retract
  7) turn driver off (00 state on pins).
  8) wait for trigger to go away? (use edge sense)

  a) drive LED while the trigger is active.

  TBD: turn latch off after some period of time versus following the trigger level.

  TBD: if trigger goes away while sequence is in progress do we maintain the latch active until the sequence completes? YES, to prevent bobbles from jamming kicker.

*/


/**
  field configurablity (EEPROM contents):
  The three timing parameters in milliseconds.
  [milliseconds],U  time between unlock and start kick
  [milliseconds],K  time between start kick and end kick
  [milliseconds],O  time between end kick and power off

  [milliseconds],D  debouncer filter time

  If cheap enough:
  pin choices and polarities

  [pin number],[0|1],L  latch
  [pin number],[0|1],F  kick out
  [pin number],[0|1],B  retract

  [pin number],[0|1],T  trigger input


  preserve settings: (NYI)
  Z

  list settings and state
  [space]

  --------------------------
  Testing conveniences


  Trigger sequence
  [-|=]

  Test light
  Q,q

  Test kicker
  [0..3] .

  All off
  x

*/

const unsigned ProgramVersion = 1;

#include "sui.h" //Simple or Serial user interface, used for debug (and someday configuration).

SUI sui(Serial, Serial);// default serial for in and out

#include "millievent.h" //millisecond timer via polling

class Actuator {
  public://letters for interactive setting interface:
    const char pinSetter;
    const char timeSetter;

  public://digital pin assignments and attributes. The usage of the time in ms varies with each pin.
    //arduino digital.. functions. Not using pinclass as we have field configurable pin assignments.
    unsigned pinNumber;
    //whether a 1 activates the feature. This allows for easy hardware mode to add an inverting buffer.
    bool highActive;
    //associated time for activation to be complete. Deactivation is Somebody Else's Problem.
    MilliTick ms;

    //for use in array init
    Actuator(const char pinSetter, const char timeSetter, unsigned pinNumber, bool highActive, unsigned ms):
      pinSetter(pinSetter), timeSetter(timeSetter), pinNumber(pinNumber), highActive(highActive), ms(ms) {
      //wait for setup() to do any actions
    }

    //allow default copy and move constructors

    bool operator =(bool active) {
      digitalWrite(pinNumber, active == highActive);
      return active;
    }

    void setup() {
      pinMode(pinNumber, timeSetter == 'T' ? INPUT_PULLUP : OUTPUT); // 'T' for trigger
      *this = false;//while this also writes to the trigger pin that does nothing
    }

    operator bool () const {
      return digitalRead(pinNumber) == highActive;
    }

};

//putting into an array for ease of nvmemory management:
Actuator pins[] = {
  {'D', 'T', 0, 0, 90},   //Trigger: debounce time
  {'U', 'L', 3, 1, 330},  //Latch: time to retract lock
  {'K', 'F', 2, 1, 560},  //MotorForward: time to guarantee full extension
  {'O', 'B', 1, 1, 680},  //MotorBackward: seemed slower on the retract
};

//we will rely upon index 0 being the only input, the trigger.
Actuator& triggerPin(pins[0]);

Actuator *actually(char key) {
  key = toupper(key); //allow for sloppy user
  for (auto &actor : pins) {
    if (actor.pinSetter == key) {
      return &actor;
    }
    //separate clauses for debug
    if (actor.timeSetter == key) {
      return &actor;
    }
  }
  return nullptr;
}

const unsigned numActuators = sizeof(pins) / sizeof(pins[0]);


#ifdef ARDUINO_SEEED_XIAO_M0
#include "digitalpin.h"
DigitalOutput debug1(PIN_LED2, 0);
DigitalOutput debug2(PIN_LED3, 0);
#else
bool debug1;
bool debug2;
#endif

/** wrapper for state machine that does the timing */
class Program {
  public: //4debug
    enum Stage {Listening, Unlocking, Kicking, Withdrawing, Done} stage;
    OneShot stepper;
    bool trigger; //debounced state of trigger

  public:

    /** apply pin configuration to pins */
    void updatePins() const {
      for (unsigned pi = numActuators; pi-- > 0;) { //we include the trigger here
        pins[pi].setup();
      }
    }

    void allOff() const {
      for (unsigned pi = numActuators; pi-- > 1;) { //we exclude the trigger
        pins[pi] = 0;
      }
    }

    void setup() {
      //todo: if eeprom has valid contents copy it to pins
      updatePins();//includes 'allOff' execution
      stage = Listening;
    }

    void startStage(Stage newstage) {
      stage = newstage;
      pins[stage] = 1;
      stepper = pins[stage].ms;
      dbg("->", pins[stage].timeSetter, " at:", MilliTicker.recent());
    }

    void step() {
      switch (stage) {
        case Listening://then we triggered
          startStage(Unlocking);
          return;

        case Unlocking://time to kick
          startStage(Kicking);
          return;

        case Kicking:
          pins[Kicking] = 0;
          startStage(Withdrawing);
          return;

        case Withdrawing:
//          pins[Withdrawing] = 0;
//          pins[Unlocking] = 0;
          startStage(Listening);//slightly abusive code, does a useless actuation
          allOff();
          return;

        default: //should never occur. If it does try to start over
          dbg("wtf in step:", stage, pins[stage].timeSetter, " at:", MilliTicker.recent());
          startStage(Listening);//slightly abusive code, does a useless actuation
          allOff();
      }
    }

    bool onTick() {
      bool rawTrigger = pins[0]; //always read so that we can reflect it to a debug pin.
      //todo: Led tracks trigger
      debug1 = rawTrigger;

      if (stage == Listening) {  //debounce input
        if (rawTrigger == trigger) { //still stable
          stepper = pins[0].ms; //repeatedly reset instead of freezing
          return false;
        } else if (stepper.isDone()) {
          trigger = rawTrigger; //record state of signal has change
          if (!trigger) {
            dbg("trigger released");
            return false; //puzzle finally reset
          } else {
            dbg("triggered");
            step();
            return true;
          }
        }
        return false;
      }

      if (stepper.isDone()) {
        //so timer has expired and either we are in one of the action states or we just saw a positive edge on the trigger
        step();
        return true;
      }

      return false;
    }

} orkules;

/********************************************************/

void setup() {
  Serial.begin(115200);//needed for most platforms
  orkules.setup();
}


/*
  Trigger sequence
  -

  Test light
  Q,q

  Test kicker
  0,1,2,3 for the drv8871 control code.
*/

void loop() {
  if (MilliTicker) { //true just once for each millisecond.
    orkules.onTick();
  }

  sui([](char key) {//the sui will process input, and when a command is present will call the following code.
    bool upper = key < 'a';
    switch (toupper(key)) {
      default:
        dbg("ignored:", unsigned(sui), ',', key);
        break;
      case ' '://status dump
        dbg.stifled = false;
        dbg("Orkules: ");
        dbg("Stage:\t", pins[orkules.stage].timeSetter);
        dbg("Timer:\t", orkules.stepper.due());
        dbg("Trigger:\t", orkules.trigger);
        break;

      case 'X':// reset state machine.
        dbg("restarting program logic");
        orkules.setup();
        break;

      case '.': {
          unsigned bits = sui;
          if (bits <= 3) {
            auto forward = actually('F');
            auto retract = actually('B');
            if (forward && retract) { //which will be true unless we have horked the config
              dbg("F/R:", bits);
              *retract = bits & 1;
              *forward = bits & 2;
            } else {
              dbg("wtf:\t output tester");
            }
          }
        } break;

      case '[': case ']': {
          auto retract = actually('L');
          if (retract) {
            *retract = key & 2;
          }
        } break;

      case 'Q':
        //todo: light on or off per case.
        debug2 = upper;
        break;

      case '-': //'-' instead of '+' for keyboarding convenience
      case '=': //same key as '+' but without shift
        orkules.step();
        break;

      case 'D': case 'U': case 'K': case 'O': //set pin config
        dbg("pin reconfiguration not yet supported");
        break;

      case 'T': case 'L': case 'F': case 'B': {//set step time
          auto activity = actually(key);
          if (activity) {
            if (sui.hasarg()) {
              activity->ms = sui;
            }
            dbg("Time ", key, " is:", activity->ms);
          } else { //serious software error
            dbg("Core Meltdown in progress!");
          }
        } break;

      case '?': case '/':
        dbg("Version:\t", ProgramVersion);
        for (unsigned pi = numActuators; pi-- > 0;) {
          const auto &cfg = pins[pi];
          dbg(pi, ":\t", cfg.timeSetter, '\t', cfg.ms, '\t', cfg.pinNumber, '\t', cfg.highActive);
        }
        break;

    }
  });
}
//end of file
