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

#include "millievent.h" //millisecond timer via polling

class Actuator {
  public://letters for interactive setting interface:
    const char timeSetter;
    const char pinSetter;

  public://digital pin assignments and attributes. The usage of the time in ms varies with each pin.
    //arduino digital.. functions. Not using pinclass as we have field configurable pin assignments.
    unsigned pinNumber;
    //whether a 1 activates the feature. This allows for easy hardware mode to add an inverting buffer.
    bool highActive;
    //associated time for activation to be complete. Deactivation is Somebody Else's Problem.
    MilliTick ms;

    //for use in array init
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
      pinMode(pinNumber,pinSetter=='T'?INPUT:OUTPUT);// 'T' for trigger
      *this = false;//while this also writes to the trigger pin that does nothing
    }

    operator bool () const {
      return digitalRead(pinNumber)==highActive;
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

const unsigned numActuators=sizeof(pins)/sizeof(pins[0]);


#ifdef ARDUINO_SEED_XIA0_M0
  #include "digitalpin.h"
  DigitalOutput debug1(LED2);
  DigitalOutput debug2(LED3);
#else
  bool debug1; 
#endif

/** wrapper for state machine that does the timing */
class Program {
  enum Stage {Listening,Unlocking,Kicking,Withdrawing,Done} stage;
  OneShot stepper;
  bool trigger;
  public:
   
    /** apply pin configuration to pins */
    void updatePins() const {
      for(unsigned pi=numActuators;pi-->0;){//we include the trigger here
        pins[pi].setup();
      }      
    }

  void allOff() const {
    for(unsigned pi=numActuators;pi-->1;){//we exclude the trigger
      pins[pi]=0;
    }
  }
   
    void setup() {
      //todo: if eeprom has valid contents copy it to pins      
      updatePins();//includes 'allOff' execution
      stage = Listening;
    }

    void startStage(Stage newstage){
        stage = newstage;
        pins[stage]=1;
        stepper=pins[stage].ms;                   
    }

    void step(){
      switch(stage){
        case Listening://then we triggered
        startStage(Unlocking);
        return;
        
      case Unlocking://time to kick
        startStage(Kicking);
        return;
        
       case Kicking:
        pins[Unlocking]=0;
        startStage(Withdrawing);                   
        return; 
       case Withdrawing:
          pins[Withdrawing]=0;
          startStage(Listening);//slightly abusive code, does a useless actuation
          return;        
      }
    }

    bool onTick() {
      bool rawTrigger=pins[0];//always read so that we can reflect it to a debug pin.
      //todo: Led tracks trigger
      debug1=rawTrigger;
          
      if (stage==Listening){
        //debounce input
        if(rawTrigger==trigger){//still stable
          stepper=pins[0].ms;//repeatedly reset instead of freezing
          return false;
        }         
      }
      //if we are listening and get here then input has been different from last stable value for quite some time
      if(stepper.isDone()) {
        if(stage==Listening){
          trigger = rawTrigger;//often gratuitous but needed some time before returning to Listening state
          if(!trigger){
            return false; //puzzle finally reset
          }
        }
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

void loop() {
  if(MilliTicker){//true just once for each millisecond. 
    orkules.onTick();
  }
}
