#include <Arduino.h> //needed by some IDE's.

#include "options.h"
/* test stepper motors.
    started with code from a clock project, hence some of the peculiar naming and the use of time instead of degrees.

    the test driver initially is an adafruit adalogger, it is what I had wired up for the clock.
    the test hardware has a unipolar/bipolar select on D17
    and power on/off D18 (need to learn the polarity )

    it is convenient that both DRV8333 and L298n take the same control input, and that pattern is such that a simple connect of the center taps to the power supply makes them operate as unipolar.

    with fixed power supply voltage the unipolar mode is twice the voltage on half the coil.
    the uni current is double, but only half as many coils are energized.

*/
////////////////////////////////////
#include "pinclass.h"
#include "cheaptricks.h"

#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial, true /*autofeed*/);

////soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"

//using microsecond soft timer to get more refined speeds.
#include "microevent.h"
#include "stepper.h"
#include "motordrivers.h" //FourBanger and similar


#ifdef ADAFRUIT_FEATHER_M0

FourBanger<10, 9, 12, 11> minutemotor;
OutputPin<17> unipolar;//else bipolar
OutputPin<18> motorpower;//relay, don't pwm this!

#elif defined(SEEEDV1_2)

FourBanger<8, 11, 12, 13> minutemotor;
bool unipolar;//else bipolar
DuplicateOutput<9, 10> motorpower; //pwm OK. These are the ENA and ENB of the L298, for stepper use there is no reason to just keep one active.

#else  //presume ProMicro/Leonardo with dual. we'll clean this up someday

#include "spibridges.h"

template<bool second> void bridgeLambda(byte phase) {
  SpiDualBridgeBoard::setBridge(second, phase);
}

void bridge0(byte phase) {
  bridgeLambda<0>(phase);
}

void bridge1(byte phase) {
  bridgeLambda<1>(phase);
}
InputPin<18, LOW> zeroMarker; //on leonardo is A0, and so on up.
InputPin<19, LOW> oneMarker; //on leonardo is A0, and so on up.

#endif

/** input for starting position of cycle. */


#ifdef LED_BUILTIN
OutputPin<LED_BUILTIN> led;
#else
bool led;
#endif

///////////////////////////////////////////////////////////////////////////
#include "char.h"

/** adds velocity to position.  */
struct StepperMotor {
  Stepper pos;
  Stepper::Step target; //might migrate into Stepper
  /// override run and step at a steady rate forever.
  bool freeRun = false;
  /** if and which direction to step*/
  int run = 0;//1,0,-1

  //time between steps
  MicroStable ticker;

  
  Stepper::Step homeWidth = 15; //todo:eeprom
  MicroStable::Tick homeSpeed = 250000; //todo:eeprom
  BoolishRef *homeSensor;

  bool which;//used for trace messages
  bool edgy;//used to detect edges of homesensor
	Stepper::Step homeOn;
  Stepper::Step homeOff;


  
  
  void setTick(MicroStable::Tick perstep) {
    ticker.set(perstep);
  }

  enum Homing { //arranged to count down to zero
    Homed = 0,   //when move to center of region is issued
    BackwardsOff,//capture far edge, so that we can center on sensor for less drift
    BackwardsOn, //capture low to high
    ForwardOff,  //if on when starting must move off
    NotHomed //setup homing run and choose the initial state
  };
  Homing homing = NotHomed;

  /** call this when timer has ticked */
  void operator()() {
    if (ticker.perCycle()) {
      bool homeChanged = homeSensor && changed(edgy, *homeSensor);
      if (homeChanged) {
        dbg("W:",which," sensor:", edgy);
      }
      switch (homing) {
        case Homed://normal motion logic
          if (!freeRun) {
            run = pos - target;//
          }
          pos += run;//steps if run !0
          break;

        case NotHomed://set up slow move one way or the other
          freeRun = 0;
          run = 0;
          if (homeSensor) {
            if (edgy) {
              pos = -homeWidth;//a positive pos will be a timeout
              target = 0;
              homing = ForwardOff;
            } else {
              pos = homeWidth;//a negative pos will be a timeout
              target = 0;
              homing = BackwardsOn;
            }
          } else {
            pos = 0;
            target = 0;
            homing = Homed;//told to home but no sensor then just clear positions and proclaim we are there.
          }
          setTick(homeSpeed);
          dbg("Homing started:", target, " @", homing, " width:", homeWidth);
          break;

        case ForwardOff://HM:3
          if (!edgy) {
            dbg("Homing backed off sensor, at ", pos);
            pos = homeWidth;//a negative pos will be a timeout
            target = 0;
            homing = BackwardsOn;
          } else {
            pos += 1;
          }
          break;

        case BackwardsOn://HM:2
          if (edgy) {
            dbg("Homing found on edge at ", pos);
            pos = homeWidth;//so that a negative pos means we should give up.
            target = 0;
            homing = BackwardsOff;
          } else {
            pos += -1;//todo: -= operator on pos.
          }
          break;

        case BackwardsOff://HM:1
          if (!edgy) {
            dbg("Homing found off edge at ", pos);
            pos = (homeWidth + pos) / 2;
            target = 0;
            homing = Homed;
            stats(&dbg);
          } else {
            pos += -1;
          }
          break;
      }
    }
  }

	//active state report, formatted as lax JSON (no quotes except for escaping framing)
  void stats(ChainPrinter *adbg) {
    if (adbg) {
      (*adbg)("{W:",which,", T:", target, ", Step:", int(pos), ", FR:", freeRun?run:0, ", HM:", homing, ", tick:", ticker.duration,'}');
    }
  }

  /** stop where it is. */
  void freeze() {
    homing = Homed;//else failed homing will keep it running.
    freeRun = false;
    target = pos;
  }

  /** not actually there until the step has had time to settle down. pessimistically that is when the next step would occur if we were still moving.*/
  bool there() const {
    return run == 0 && target == pos;
  }

  bool atIndex() {
    freeze();
    pos = 0;
  }

  void start(bool second, Stepper::Interface iface, BoolishRef *homer) {
  	which = second;
    pos.interface = iface;
    if (changed(homeSensor, homer)) {
      if (homeSensor != nullptr) {
        homing = NotHomed;
      } else {
        homing = Homed;
      }
    }
    setTick(homeSpeed);
  }
};

StepperMotor motor[2];//setup will attach each to pins/

///* run fast in direction controlled by button. */
//template <PinNumberType forward, PinNumberType reverse> class SlewControl {
//  public: //for diagnostics
//    InputPin<forward> fwd;
//    InputPin<reverse> rev;
//  public: //easier to set after construction.
//    ClockHand &myHand;
//    SlewControl(ClockHand & myHand): myHand(myHand) {
//      //#set myHand on setup()
//    }
//
//    void onTick() { //every millisecond
//      if (fwd) {
//        myHand.freerun = +1;
//        myHand.upspeed ( slewspeed);
//      } else if (rev) {
//        myHand.freerun = -1;
//        myHand.upspeed ( slewspeed);
//      } else {
//        myHand.realtime();
//      }
//    }
//};


//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP<MicroStable::Tick> cmd;//need to accept speeds, both timer families use 32 bit values.

//I2C diagnostic
#include "scani2c.h"

void doKey(char key) {
  Char k(key);
  bool which = k.isLower();//which of two motors
  if (which) {
    k.raw -= ' '; //crass way to do a 'toupper'
  }

  switch (k) {
    case ' '://report status
      motor[0].stats(&dbg);
      motor[1].stats(&dbg);
      break;

    case 'M'://go to position
      motor[which].target = cmd.arg;
      break;

    case 'N'://go to negative position (present cmd processor doesn't do signed numbers, this is first instance of needing a sign )
      motor[which].target = -cmd.arg;
      break;

    case 'H'://takes advantage of 0 not being a viable value for these optional parameters
      if (cmd.pushed) {//if 2 args width,speed
        motor[which].homeWidth = cmd.pushed;
        dbg("set home width to ", motor[which].homeWidth);
      }
      if (cmd.arg) {//if 1 arg speed.
        motor[which].homeSpeed = cmd.arg;
        dbg("set home speed to ", motor[which].homeSpeed);
      }
      motor[which].homing = StepperMotor::NotHomed;//will lock up if no sensor present or is broken.
      dbg("starting home procedure at stage ", motor[which].homing);
      break;

    case 'K':
      motor[which].target -= 1;
      break;
    case 'J':
      motor[which].target += 1;
      break;

    case 'Z'://declare present position is ref
      dbg("marking start");
      motor[which].pos = 0;
      break;

    case 'S'://set stepping rate to use for slewing
      dbg("Setting slew:", cmd.arg);
      motor[which].setTick(cmd.arg);
      break;

    case 'X': //stop stepping
      dbg("Stopping.");
      motor[which].freeze();
      break;

    //one test system had two relays for switching the motor wiring:
    //  case 'U':
    //    dbg("unipolar engaged");
    //    unipolar = true;
    //    break;
    //  case 'B':
    //    dbg("bipolar engaged");
    //    unipolar = false;
    //    break;

    case 'P':
      dbg("power on");
      SpiDualBridgeBoard::power(which, true);
      break;

    case 'O':
      dbg("power off");
      SpiDualBridgeBoard::power(which, false);
      break;

    case 'R'://free run in reverse
      dbg("Run Reverse.");
      motor[which].run = -1;
      motor[which].freeRun = true;
      break;

    case 'F'://run forward
      dbg("Run Forward.");
      motor[which].run = +1;
      motor[which].freeRun = true;
      break;

    case 'G'://whatever
      dbg("inputs:", motor[0].edgy, ",", motor[1].edgy);
      break;

    case '#':
      switch (cmd.arg) {
        case 42://todo: save initstring to eeprom
          dbg("save initstring to eeprom not yet implemented");//this message reserves bytes to do so :)
          break;
        case 6://todo: read initstring from eeprom
          dbg("read initstring from eeprom not yet implemented");//this message reserves bytes to do so :)
          break;
        default:
          dbg("# takes 42 to burn eeprom, 6 to reload from it, else ignored");
          break;
      }
      break;
    case '?':
      scanI2C(dbg);
#if UsingEDSir
      dbg("IR device is ", IRRX.isPresent() ? "" : "not", " present");
#endif
      break;

    default:
      dbg("Ignored:", char(key), " (", key, ") ", cmd.arg, ',', cmd.pushed);
      //      return; //don't put in trace buffer
  }
}

//keep separate, we will feed from eeprom as init technique
void accept(char key) {
  if (cmd.doKey(key)) {
    doKey(key);
  }
}

void doui() {
  while (auto key = dbg.getKey()) {
    accept(key);
  }
}

void doString(const char *initstring) {
  while (char c = *initstring++) {
    accept(c);
  }
}

/////////////////////////////////////

void setup() {
  Serial.begin(115200);
  dbg("setup");
  SpiDualBridgeBoard::start(true);//using true during development, low power until program is ready to run

  motor[0].start(0, bridge0, &zeroMarker);
  doString("15,250000h");
  motor[1].start(1, bridge1, &oneMarker);
  doString("15,250000H");
}


void loop() {

  if (MicroTicked) {//on avr this is always true, cortexm0, maybe 50%
    motor[0]();
    motor[1]();
  }
  if (MilliTicked) {//non urgent things like debouncing index sensor
    led = zeroMarker ^ oneMarker;
    doui();
  }
}
