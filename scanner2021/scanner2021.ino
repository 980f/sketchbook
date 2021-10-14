

/*****
   todo: count triggers and report on number of failed scans
*/

#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

//we will want to delay some activities, such as changing motor direction.
#include "millievent.h"
#include "digitalpin.h"

//switches on leadscrew/rail:
DigitalInput nearend(2,  LOW);  //BROKEN
DigitalInput farend(3, LOW);
//switch in command center
DigitalInput triggerIn(5, LOW);

#include "edgyinput.h"
EdgyInput homesense(farend.number, 3); //seems to bounce on change
EdgyInput awaysense(nearend.number);//BROKEN HARDWARE
EdgyInput trigger(triggerIn.number, 7);

//lights are separate so that we can force them on as work lights, and test them without motion.
DigitalOutput lights(14, LOW); // ssr White
//2 for the motor
DigitalOutput back(17); //a.k.a go towards home
DigitalOutput away(19); //a.k.a go away from home

//not yet sure what else we will do
DigitalOutput other(15, LOW);
//NOTE: relays 16,18 are broken
//////////////////////////////////////////////////
//finely tuned parameters
static const MilliTick Breaker = 250;  //time to ensure one relay has gone off before turning another on
static const MilliTick Maker = 100;   //time to ensure relay is on before trusting that action is occuring.
static const MilliTick Scantime = 11400;  //time from start to turnaround point
static const MilliTick TriggerDelay = 5310; //taste setting, time from button press to scan start.

//made variable for debug:
static MilliTick Scanmore = 0; //extra time used on first out and last back
static unsigned numPasses = 2;

void debugValues() {
  dbg("passes (n): ", numPasses, " hysteresis(h): ", Scanmore);
}
//////////////////////////////////////////////////

struct StateParams {
  bool lights;
  bool back;
  bool away;
  MilliTick delaytime;
};

StateParams stab[] {
  //L, back, away
  {0, 0, 0, Breaker}, //powerup
  {0, 1, 0, Scantime}, //homing
  {0, 0, 0, BadTick}, //idle
  {1, 0, 0, TriggerDelay}, //triggered  delay is from trigger active to scan starts
  {1, 0, 1, Scantime}, //run away
  {1, 0, 0, Breaker}, //stopping  //can turn both off together, no race
  {1, 1, 0, Scantime}, //running home, a little extra to tend to drift.
  {1, 0, 0, Breaker}, //got home  //!!! actually means didn't get all the way home!
  {1, 0, 0, Maker}, //another_pass
  {1, 0, 0, Breaker}, //home_early  //actually this is the prefered/normal way to get home
  {0, 0, 0, Maker}, //chill, ignores trigger until this expires
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
    unsigned passcount = 0;
  public:
    bool freeze = false;

    void enterState(MotionState newstate, const char *reason, bool more = false) {
      StateParams &params = stab[newstate];
      //fyi: these assignments actually manipulate hardware:
      back = params.back;
      away = params.away;
      lights = params.lights;
      //back to normal variables.
      timer = params.delaytime + (more ? Scanmore : 0); //have to add scanmore dynamically to appease debug access
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
      bool amhome = homesense; //read level once since we are debouncing off of this read
      if (homesense.changed()) {
        debugSensors();
      }

      bool triggerEvent = trigger; //read level once since we are debouncing off of this read
      if (trigger.changed()) {
        debugSensors();
      }

      if (freeze) {
        return;
      }

      bool lastpass = passcount >= numPasses;
      switch (activity) {
        case powerup:
          if (triggerEvent) {
            if (amhome) {
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
          } else if (!amhome) { //todo: if not at home we are fucked?
            enterState(powerup, " lost home");
          }
          break;
        case triggered:
          if (amdone) {
            enterState(run_away, " time done", true);
          }
          break;
        case run_away:
          if (amdone) {
            enterState(stopping, " time done");
          }
          break;

        case stopping:
          if (amdone) {
            enterState(running_home, " time done", lastpass);
          }
          break;

        case running_home:
          if (amhome) { //then we are done with this part of the effect.
            enterState(home_early, " found home switch");
          } else if (amdone) {//we got stuck in the middle
            enterState(powerup, " timed out home sense");
          }
          break;

        case got_home:
        case home_early://todo: fix this when sensor works
          if (amdone) {
            if (lastpass) {
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
      if (back && amhome) { //safeguard
        back = 0;
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
  dbg(farend ? 'H' : 'h', triggerIn ? 'T' : 't', '\t', " DEB:", bool(homesense) ? 'H' : 'h', homesense.debouncer , trigger ? 'T' : 't', '\t', statetext[Motion.activity]);
}

void debugOutputs() {
  dbg(back ? 'B' : 'b', away ? 'A' : 'a', lights ? 'L' : 'l', other ? 'O' : 'o', '\t', statetext[Motion.activity]); //two more for flicker lights?
}


////////////////////////////////////////////////

void setup() {
  //todo: read timing values from EEprom
  Serial.begin(115200);
  dbg("-------------------------------");
  homesense.begin();
  awaysense.begin();
  trigger.begin();

  debugSensors();
  debugOutputs();
  debugValues();
  Motion.freeze = false;
  Motion.activity = oopsiedaisy; //somehow run_away was being reported
  Motion.enterState(powerup, "RESTARTED");


  Motion.freeze = true;

  for (unsigned lc = 10; lc-- > 0;) {
    bool amhome = homesense; //read level once since we are debouncing off of this read
    if (homesense.changed()) {
      dbg("Loop-h: ", 10 - lc);
      debugSensors();
    }

    bool triggerEvent = trigger; //read level once since we are debouncing off of this read
    if (trigger.changed()) {
      dbg("Loop-t: ", 10 - lc);
      debugSensors();
    }

  }

}

/** streams kept separate for remote controller, where we do cross the streams. */
#include "unsignedrecognizer.h"

UnsignedRecognizer<unsigned> numberparser;
//for 2 parameter commands, gets value from param.
unsigned arg = 0;
unsigned pushed = 0;

void doKey(char key) {
  if (key == 0) { //ignore nulls, might be used for line pacing.
    return;
  }
  //test digits before ansi so that we can have a numerical parameter for those.
  if (numberparser(key)) { //part of a number, do no more
    return;
  }
  arg = numberparser; //read and clear, regardless of whether used.
  switch (tolower (key)) {
    case 27://escape key
      Motion.freeze = true; //USE SETUP TO CLEAR THIS
      break;

    case '$':
      Motion.enterState(homing, " $ key");
      break;
    case '@':
      setup();
      break;
    case 'n':
      numPasses = arg;
      break;
    case 'h':
      Scanmore = arg;
      break;
    case 't': case 'T':
      Motion.enterState(triggered, "t key");
      break;
    case 'f': case 'F':
      back = 0;
      away = 1;
      break;
    case 'b': case 'B':
      away = 0;
      back = 1;
      break;
    case 'r': case 'R':
      // scan = 1;
      break;
    case 'e': case 'E':
      away = 0;
      back = 0;
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
    case '/':
      debugValues();
      break;

    //////////////////////////////////////
    default: //any unknown key == panic
      dbg(" panic!");
      back = 0;
      away = 0;
      lights = 0;
      break;
  }
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
      doKey(key);
    }
  }
}
