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

  TBD: turn latch off after some period of time rather than following the trigger level.

  TBD: if trigger goes away while sequence is in progress do we maintain the latch active until the sequence completes?

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


  preserve settings:
  Z

  list settings and state
  [space]

  --------------------------
  Testing conveniences


  Trigger sequence
  -

  Test light
  Q,q

  Test kicker
  <,>

  All off
  x

*/


class Actuator {
  public://for rpn engine access?
    const char timeSetter;
    const char pinSetter;

  public:
    //arduino digital.. functions
    unsigned pinNumber;
    //associated time for activation to be complete. Deactivation is Somebody Else's Problem.
    unsigned ms;
    //whether a 1 activates the feature. This allows for easy hardware mode to add an inverting buffer.
    bool highActive;
    //tokens for field configuration:
    
    Actuator(  const char timeSetter,  const char pinSetter,    unsigned pinNumber,  bool highActive,  unsigned ms):
      timeSetter(timeSetter), pinSetter(pinSetter),  pinNumber(pinNumber),  ms(ms),  highActive(highActive) {
      //wait for setup() to do any actions
    }
    //allow default copy and move constructors

    bool operator =(bool active) {
      digitalWrite(pinNumber, active == highActive);
      return active;
    }

    void setup() {
      //todo: configure pin
      //todo: set to inactive level
    }

    void configure() {
      //todo: mate to RPN input machine
    }

};

Actuator pins[] = {
  {'U', 'L', 3, 1, 330},
};

class Action {
  public:
    const Actuator &actuator;
    bool actuation;//true/1 for on, false/0 for off

};

class Program {

  public:
    unsigned doneAt;//timer
    void setup() {
      //todo: don't trust compiler generated init.

    }

    bool onTick() {
      //todo: on timer expired go to next step
      if (doneAt != ~0 && doneAt--) {
        //do nothing
        return false;
      }

      //maydo: abort sequence if trigger goes to zero.
      //debounce trigger input
    }
} sequencer;

/********************************************************/

void setup() {
  //for each actuator
  //the state machine

}

void loop() {
  //if it has been a millisecond call the Program onTick()

}
