

/*****
   done via changing default of digitalpin class: pullup switch inputs
   todo: count triggers and report on number of failed scans
*/

#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

//we will want to delay some activities, such as changing motor direction.
#include "millievent.h"
#include "digitalpin.h"

bool sensorWorks = 0;

//switches on leadscrew/rail:
DigitalInput nearend(2,  LOW);
DigitalInput farend(3, LOW);
//switch in command center
DigitalInput triggerIn(5, LOW);

#include "edgyinput.h"
EdgyInput homesense(farend);
EdgyInput awaysense(nearend);
EdgyInput trigger(triggerIn);

//lights are separate so that we can force them on as work lights, and test them without motion.
DigitalOutput lights(16);
//2 for the motor
DigitalOutput scan(17); //a.k.a run
DigitalOutput away(18, LOW); //a.k.a direction

//not yet sure what else we will do
DigitalOutput other(19);
//////////////////////////////////////////////////
//finely tuned parameters
static const MilliTick Breaker = 75;  //time to ensure one relay has gone off before turning another on
static const MilliTick Maker = 100;   //time to ensure relay is on before trusting that action is occuring.
//full rail jams the light cord:static const MilliTick Fullrail = 10000; //timeout for homing, slightly greater than scan
static const MilliTick Scantime = 11400;  //time from start to turnaround point
static const MilliTick Scanmore = 100; //time from turnaround back to home, different from above due to hysterisis in both motion and the home sensor
static const MilliTick TriggerDelay = 5310; //taste setting, time from button press to scan start.
static const unsigned numPasses = 2;
//////////////////////////////////////////////////

struct StateParams {
  bool lights;
  bool scan;
  bool away;
  MilliTick delaytime;
};

StateParams stab[] {
  //L, S, away
  {0, 0, 0, Breaker}, //powerup
  {0, 1, 0, Scantime + Scanmore}, //homing
  {0, 0, 1, BadTick}, //idle
  {1, 0, 1, TriggerDelay}, //triggered  delay is from trigger active to scan starts
  {1, 1, 1, Scantime}, //run away
  {1, 0, 0, Breaker}, //stopping  //can turn both off together, no race
  {1, 1, 0, Scantime + Scanmore}, //running home, a little extra to tend to drift.
  {1, 0, 0, Breaker}, //got home  //!!! actually means didn't get all the way home!
  {1, 0, 1, Maker}, //another_pass
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
  another_pass,
  home_early,
  chill,

  oopsiedaisy //should never get here!
};


const char  * const statetext[] = {
  "powerup", //natural value for power up of program
  "homing",
  "idle",
  "triggered",
  "run_away",
  "stopping",
  "running_home",
  "got_home",
  "another_pass",
  "home_early",
  "chill",

  "oopsiedaisy" //should never get here!

};

class MotionManager {
  public: //for debug output
    MonoStable timer;
    MonoStable since;
    MotionState activity;
    unsigned passcount;
  public:
    //if we need multiple passes: unsigned scanpass = 0;
    void enterState(MotionState newstate, const char *reason) {
      StateParams &params = stab[newstate];
      //fyi: these assignments actually manipulate hardware:
      scan = params.scan;
      away = params.away;
      lights = params.lights;
      //back to normal variables.
      timer = params.delaytime;
      dbg("To ", statetext[newstate], " From: ", statetext[activity], " due to:", reason, " at ", since.elapsed());
      if (newstate == run_away) {
        ++passcount;
      }
      activity = newstate;
      since.start();
    }

    void onTick() {
      //read event inputs once for programming convenience
      bool amdone = timer.hasFinished(); //which disables timer if it is done.
      bool amhome = homesense; //read level once for logical coherence since we aren't debouncing
      bool triggerEvent = triggerIn;// trigger.changed() && triggerIn;//also an edge sense and we only care about activation,
      switch (activity) {
        case powerup:
          if (triggerEvent) {
            if (!sensorWorks || amhome) {
              enterState(triggered, " first trigger");
            } else {
              enterState(homing, " first trigger");
            }
          }
          break;
        case homing:
          if (amhome) {
            enterState(idle, " homed OK");
          } else if (amdone) {
            //we got stuck in the middle
            enterState(powerup, " failed to home");
          }
          break;
        case idle: //normal starting point
          passcount = 0;
          if (triggerEvent) {
            enterState(triggered, " trigger input");
          } else if (sensorWorks && !amhome) { //todo: if not at home we are fucked?
            enterState(powerup, " lost home");
          }
          break;
        case triggered:
          if (amdone) {
            enterState(run_away, " time done");
          }
          break;
        case run_away:
          if (amdone) {
            enterState(stopping, " time done");
          }
          break;

        case stopping:
          if (amdone) {
            enterState(running_home, " time done");
          }
          break;

        case running_home:
          if (sensorWorks && amhome) { //then we are done with this part of the effect.
            enterState(home_early, " found home switch"); //todo: rename state, it is normal- not early
          } else if (amdone) {//we got stuck in the middle
            enterState(got_home, " timed out");
          }
          break;

        case got_home:
        case home_early://todo: fix this when sensor works
          if (amdone) {
            if (passcount >= numPasses) {
              enterState(chill, " scan completed");
            } else {
              enterState(run_away, " more passes");
            }
          }
          break;

        case chill:
          if (amdone) { //only true if timer was both relevant and has completed
            enterState(idle, " scan completed");
          }
          break;
        default:
          enterState(powerup, " missing case statement");
          break;
      }
      //home seems incoherent
      if (sensorWorks && scan && !away && amhome) { //safeguard
        scan = 0;
        //todo: we should go to some state
        enterState(idle, " home unexpectedly");
      }
    }
};

MotionManager Motion;

////////////////////////////////////////////////
// mostly just debug and arduino boilerplate below here.

/** peek at sensor inputs */
void debugSensors() {
  dbg(farend ? 'H' : 'h', nearend ? 'F' : 'f', triggerIn ? 'T' : 't', '\t', statetext[Motion.activity]);
}

void debugOutputs() {
  dbg(scan ? 'S' : 's', away ? 'A' : 'a', lights ? 'L' : 'l', other ? 'O' : 'o', '\t', statetext[Motion.activity]); //two more for flicker lights?
}


////////////////////////////////////////////////

void setup() {
  //todo: read timing values from EEprom
  Serial.begin(115200);
  dbg("-------------------------------");
  debugSensors();
  debugOutputs();
  Motion.enterState(powerup, "RESTARTED"); //don't trust C init.
}

void loop() {
  if (MilliTicked) { //nothing need be fast and the processor may be in a tight enclosure
    Motion.onTick();
    //audio.onTick();
    //todo: other timers may expire and here is where we service them

  }

  //if serial do some test thing
  if (Serial) {
    auto key = Serial.read();
    if (key > 0) {
      dbg("key:", char(key), '(', key, ')');
      switch (key) {
        case '$':
          Motion.enterState(homing, " $ key");
          break;
        case 't': case 'T':
          Motion.enterState(triggered, "t key");
          break;
        case 'f': case 'F':
          away = 1;
          break;
        case 'b': case 'B':
          away = 0;
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
        case ',':
          debugOutputs();
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
