

/*****
   main module for hallway scanner built with garage door opener.
   8dec2021 or so this was updated to deal with library changes without having a system to test it on.
   The hallway stuff never got tested.

   todo:1 count triggers and report on number of failed scans
*/

#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

//we will want to delay some activities, such as changing motor direction.
#include "millievent.h"
#include "edgypin.h"  //edge sensitive polling of pins, with debounce

//switches on leadscrew/rail:
EdgyPin homesense(3, LOW, 3); //seems to bounce on change
EdgyPin awaysense(2,  LOW, 0); //BROKEN HARDWARE! we tune time to never get to this end.
EdgyPin trigger(5, LOW, 4);

//lights are separate so that we can force them on as work lights, and test them without motion.
DigitalOutput lights(14, LOW); // ssr White

//2 for the motor
DigitalOutput back(17); //a.k.a go towards home
DigitalOutput away(19); //a.k.a go away from home

//not yet sure what else we will do
DigitalOutput other(15, LOW);
//NOTE: relays 16,18 are broken. 14,15 are now wired to SSR outlet box for scanner light and one hallway light
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
#include "flickery.h"
/** flicker 'other' relay for some periods of time after a delay */
class HallwayManager {
    enum {triggerDelay = 7000, flickerfor = 2300, waitafter = 2021};
    unsigned state = 0;

  public:
    OneShot timer;
    Flickery erratic{250, 150, 150}; //flicker slowly until we learn what the lamp can handle

  public:
    void onTick(bool triggerEvent) {
      bool amdone = timer; //which disables timer if it is done.

      switch (state) {
        case triggerDelay:
          if (amdone) {
            state = flickerfor;
            timer = flickerfor;
          }
          break;
        case flickerfor:
          if (amdone) {
            state = waitafter;
            timer = waitafter;

          } else {
            erratic.onTick();
            other = erratic;
          }
          break;
        case waitafter:
          if (amdone) {
            state = 0;
          }
          break;
        default: //FYI should only be zero.
          if (triggerEvent) {
            state = triggerDelay;
            timer = triggerDelay;
          }
          break;
      }
    }

};

HallwayManager Hallway;

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
    OneShot timer;
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

    void onTick(bool triggerEvent) {
      //read event inputs once for programming convenience
      bool amdone = timer; //which disables timer if it is done.
      bool amhome = homesense.onTick() && homesense; //presently onTick() gives any change, should add 3 level option into the edgy class
      //triggerEvent passed in now as it is shared by Hallway machine and we reset the edge detect when we sample it.
      bool lastpass = passcount >= numPasses;

      if (amdone) {
        dbg("Timer finished ", MilliTick(timer));
      }

      if (freeze) {
        if (amdone) {
          dbg("Ignoring timer, frozen");
        }
        return;
      }

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
        back = false;
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
  dbg("Pins:", trigger.raw() ? 'T' : 't', '\t', homesense.raw() ? 'H' : 'h', "\tDEB:", trigger ? 'T' : 't', '\t', statetext[Motion.activity]);
}

void debugOutputs() {
  dbg("Outputs: ", back ? 'B' : 'b', away ? 'A' : 'a', lights ? 'L' : 'l', other ? 'O' : 'o', '\t', statetext[Motion.activity]);
}


////////////////////////////////////////////////

void setup() {
  //todo: read timing values from EEprom and configure debouncing of inputs vs. prresent static config.
  Serial.begin(115200);
  dbg("-------------------------------");
  homesense.begin();
  awaysense.begin();
  trigger.begin();
  //report some stuff at reset, as much to indicate reset as to be informative of the details.
  debugSensors();
  debugOutputs();
  debugValues();

  //let the activities begin:
  Motion.freeze = false;
  Motion.activity = oopsiedaisy; //somehow run_away was being reported
  Motion.enterState(powerup, "RESTARTED");
}

/** streams kept separate for remote controller, where we do cross the streams. */
#include "unsignedrecognizer.h"  //todo: use clirp instead of these pieces cut from the middle of it.
UnsignedRecognizer<unsigned> numberparser;
unsigned arg = 0;

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
      dbg("freezing motion");
      Motion.freeze = true; //USE SETUP TO CLEAR THIS
      away = false;
      back = false;
      break;
    case '`':
      dbg("MS: ", statetext[Motion.activity], " left: ", Motion.timer.due(), (Motion.timer.isRunning() ? " R " : " X "), "since: ", Motion.since.elapsed());
      break;
    case '$':
      Motion.freeze = false;
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
      //todo: also trigger hallway, or give it its own letter
      break;
    case 'f': case 'F':
      back = false;
      away = true;
      break;
    case 'b': case 'B':
      away = false;
      back = true;
      break;
    case 'r': case 'R':
      // scan = 1;
      break;
    case 'e': case 'E':
      away = false;
      back = false;
      break;
    case 'l': case 'L':
      lights = true;
      break;
    case 'o': case 'O':
      lights = false;
      break;
    case 'q': case 'Q':
      other = true;
      break;
    case 'a': case 'A':
      other = false;
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
      dbg("ignored:", char(key), '(', key, ')');
      break;
  }
}


void loop() {
  if (MilliTicked) { //nothing need be fast and the processor may be in a tight enclosure
    //sample shared pins here:
    bool triggerEvent =     trigger.onTick() && trigger; //did it just fire?
    //update state machines:
    Motion.onTick(triggerEvent);
    Hallway.onTick(triggerEvent);
  }

  //if serial do some test thing
  if (Serial) {
    auto key = Serial.read();
    if (key > 0) {
      doKey(key);
    }
  }
}
//end of main driver for hallway scanner.
