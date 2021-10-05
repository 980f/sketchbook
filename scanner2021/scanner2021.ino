

/*****
   todo: pullup switch inputs
   todo: count triggers and report on number of failed scans
*/

#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

//disappeared from libs: #include "cheaptricks.h"
//we will want to delay some activities, such as changing motor direction.
#include "millievent.h"
#include "digitalpin.h"

//switches on leadscrew/rail:
DigitalInput nearend(2, LOW);
DigitalInput farend(3, LOW);
DigitalInput trigger(5, LOW);

#include "edgyinput.h"
EdgyInput homesense(farend);
EdgyInput awaysense(nearend);

//and how do we deal with trigger?

//lights are separate so that we can force them on as work lights, and test them without motion.
DigitalOutput lights(16);
//2 for the motor
DigitalOutput scan(17); //a.k.a run
DigitalOutput away(18); //a.k.a direction

//not yet sure what else we will do
DigitalOutput other(19);
//////////////////////////////////////////////////
//finely tuned parameters
static const MilliTick Breaker = 75;  //time to ensure one relay has gone off before turning another on
static const MilliTick Maker = 100;   //time to ensure relay is on before trusting that action is occuring.
static const MilliTick Fullrail = 10000; //timeout for homing, slightly greater than scan
static const MilliTick Scanout = 9000;  //time from start to turnaround point
static const MilliTick Scanback = 9200; //time from turnaround back to home, different from above due to hysterisis in both motion and the home sensor
static const MilliTick TriggerDelay = 5310; //taste setting, time from button press to scan start.
//////////////////////////////////////////////////

struct StateParams {
  bool lights;
  bool scan;
  bool away;
  MilliTick delaytime;
};

StateParams stab[] {
  {0, 0, 0, Breaker}, //powerup
  {0, 1, 0, Fullrail}, //homing
  {0, 0, 1, BadTick}, //idle
  {1, 0, 1, TriggerDelay}, //triggered  delay is from trigger active to scan starts
  {1, 1, 1, Scanout}, //run away
  {1, 0, 0, Breaker}, //stopping  //can turn both off together, no race
  {1, 1, 0, Scanback}, //running home
  {1, 0, 0, BadTick}, //got home  //!!! actually means didn't get all the way home!
  {1, 0, 0, Breaker}, //home_early  //actually this is the prefered/normal way to get home
  {0, 0, 1, Maker}, //chill, ignores trigger until this expires
  //we go to idle from above
  {0, 0, 0, BadTick}, //unused/guard table

};


//program states, above are hardware state
enum MotionState {
  powerup = 0, //natural value for power up of program
  homing,
  idle,
  triggered,
  run_away,
  stopping,
  running_home,
  got_home,
  home_early,
  chill,

  oopsiedaisy //should never get here!
};

class MotionManager {
    MonoStable timer;
    MotionState activity;
  public:
    //if we need multiple passes: unsigned scanpass = 0;
    void enterState(MotionState newstate) {
      StateParams &params = stab[newstate];
      scan = params.scan;
      away = params.away;
      timer = params.delaytime;
      activity = newstate;
    }

    void onTick() {
      //read event inputs once for programming convenience
      bool amdone = timer.hasFinished(); //which disables timer
      bool amhome = homesense;

      switch (activity) {
        case idle: //must be here to honor trigger
          if (trigger) {
            enterState(triggered);
          }
          //todo: if not at home we are fucked?
          break;
        case running_home:
          if (amhome) { //then we are done with this part of the effect.
            enterState(home_early);//todo: rename state
          } else if (amdone) {//we got stuck in the middle
            enterState(homing);
            //todo: deal with this error!
            dbg("Short!");
          }
          break;

        case homing:
          if (amhome) {
            enterState(idle);
          } else if (amdone) {
            //we got stuck in the middle
            enterState(oopsiedaisy);//todo: deal with this error!
          }
          break;
        default:
          if (amdone) { //only true if timer was both relevant and has completed
            enterState(activity + 1);
          }
          break;
      }
    }
};

MotionManager Motion;

/** peek at sensor inputs */
void debugSensors() {
  dbg(farend ? 'H' : 'h', nearend ? 'F' : 'f', trigger ? 'T' : 't');
}

void debugOutputs() {
  dbg(scan ? 'S' : 's', away? 'A' : 'a', lights? 'L' : 'l', other ? 'O':'o');//two more for flicker lights?
}


////////////////////////////////////////////////

void setup() {
  //todo: read options from EEprom
  Serial.begin(115200);
}

void loop() {
  if (MilliTicked) { //nothing need be fast and the processor may be in a tight enclosure
    Motion.onTick();
    //audio.onTick();
    //todo: other timers may expire and here is where we service them

  }

  //todo: if serial do some test thing
  if (Serial) {
    auto key = Serial.read();
    if (key > 0) {
      dbg("key:", key);
      switch (key) {
        case 't': case 'T':
          Motion.enterState(triggered);
          break;
        case 'f': case 'F':
          away = 0;
          break;
        case 'b': case 'B':
          away = 1;
          break;
        case 'r': case 'R':
          scan = 1;
          break;
        case 'e': case 'E':
          scan = 0;
          break;
        case 'l': case 'L':
          lights = 1;
          break;
        case 'o': case 'O':
          lights = 0;
          break;
        case 'q': case 'Q':
          other = 1;
          break;
        case 'a': case 'A':
          other = 0;
          break;
        case ' ':
          debugSensors();
          break;
        //////////////////////////////////////
        default: //any unknown key == panic
          dbg(" panic!");
          scan = 0;
          away = 0;
          lights = 0;
          break;
      }
    }
  }
}
