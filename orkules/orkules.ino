/**
 * noEEPROM branch is a stripped down version with no field configurability or debuggability
 * the program that shipped is the commit before the noEEPROM branch was made.
 * 
 
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


const unsigned ProgramVersion = 2;

#include "dbgserial.h"

#include "millievent.h" //millisecond timer via polling

/* a class for output/control pins.
   it wraps Arduino function oriented syntax so that activating a pin is done via assigning a 1, deactivating by assigning a 0
   it has the concept of 'active level' such that if we have to stick in an inverting buffer the only change to the source code
   is in the pin constructor, not each place of use.
   It includes a "time required to actually do its job" parameter. */
class Actuator {

  public://digital pin assignments and attributes. The usage of the time in ms varies with each pin.
    //arduino digital.. functions. Not using 980f's digitalpin class as we have field configurable pin assignments.
    const unsigned pinNumber;
    //whether a 1 activates the feature. This allows for easy hardware mode to add an inverting buffer.
    const bool highActive;
    //associated time for activation to be complete. Deactivation is Somebody Else's Problem.
    const MilliTick ms;

    //for use in array init
    constexpr Actuator(unsigned pinNumber, bool highActive, unsigned ms): pinNumber(pinNumber), highActive(highActive), ms(ms) {
      //wait for setup() to do any actions, so that we can constexpr construct items.
    }

    //allow default copy and move constructors

		//assignment is how you change the state of the control
    bool operator =(bool active) const {
      digitalWrite(pinNumber, active == highActive);
      return active;
    }

		//tell Arduino support library what we are doing with this pin, also deactivates the pin.
    void setup(bool asTrigger) const {
      pinMode(pinNumber, asTrigger ? INPUT_PULLUP : OUTPUT); // 'T' for trigger
      *this = false;//while this also writes to the trigger pin that does nothing
    }

		//report whether it is activated.
    operator bool () const {
      return digitalRead(pinNumber) == highActive;
    }

};

//putting into an array for ease of nvmemory management:
//not const so that we can reconfigure them on the fly, if we ever actually implement using EEPROM for configuration, useful if we burn out a pin in the field.
const Actuator pins[] = {
  { 0, 0, 90},   //Trigger: debounce time
  { 3, 1, 330},  //Latch: time to retract lock
  { 2, 1, 560},  //MotorForward: time to guarantee full extension
  { 1, 1, 680},  //MotorBackward: seemed slower on the retract
};
const unsigned numActuators = sizeof(pins) / sizeof(pins[0]);

//we will rely upon index 0 being the only input, the trigger.
const Actuator& triggerPin(pins[0]);


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
    enum Stage {Listening, Unlocking, Kicking, Withdrawing, Done} stage;//N.B.: these are used as indexes into the pins[] array of actuators!
    OneShot stepper;
    bool trigger; //debounced state of trigger

  public:

    /** apply pin configuration to pins */
    void updatePins() const {
      for (unsigned pi = numActuators; pi-- > 0;) { //we include the trigger here
        pins[pi].setup(pi==Listening);//Listening 'activates' the trigger input
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
      dbg("->",stage, " at:", MilliTicker.recent());
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
          startStage(Listening);//slightly abusive code, does a useless actuation in addition to doing what is commented out above.
          allOff();
          return;

        default: //should never occur. If it does try to start over
          dbg("wtf in step:", stage," at:", MilliTicker.recent());
          startStage(Listening);//slightly abusive code, does a useless actuation
          allOff();
      }
    }

		//call this once per millisecond
    bool onTick() {
      bool rawTrigger = pins[0]; //always read so that we can reflect it to a debug pin.
      debug1 = rawTrigger;  //## ignore spurious compiler warning, it gets upset because of a supposed ambiguity between what we want here and a deleted thing.

      if (stage == Listening) {  //debounce input
        if (rawTrigger == trigger) { //still stable
          stepper = pins[0].ms; //repeatedly reset instead of freezing
          return false;
        } else if (stepper.isDone()) {
          trigger = rawTrigger; //record that state of signal has change
          if (!trigger) {
            dbg("trigger released");
            return false; //the puzzle finally reset
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

} orkules; //one and only one puzzle per processor.

/********************************************************/

void setup() {
  Serial.begin(115200);//needed for most platforms, can be a problem on some which use USB native for Serial and may not function unless connected to a USB host.
  orkules.setup();
}

void loop() {
  if (MilliTicker) { //true just once for each millisecond.
    orkules.onTick();
  }
}
//end of file
