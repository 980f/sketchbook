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

#include "microevent.h"
#include "stepper.h"
#include "motordrivers.h" //FourBanger and similar
//
////project specific values:
////steps per rev matters only when seeking zeroMarker, might be handy for UI.
//const unsigned baseSPR = 200;//1.8' steppers ar ethe ones that need speed testing
//
////todo: code in seconds so that we can switch between micro and milli
//RawMicros slewspeed = 500000;


#ifdef ADAFRUIT_FEATHER_M0
FourBanger<10, 9, 12, 11> minutemotor;
OutputPin<17> unipolar;//else bipolar
OutputPin<18> motorpower;//relay, don't pwm this!
#elif defined(SEEEDV1_2)
FourBanger<8, 11, 12, 13> minutemotor;
bool unipolar;//else bipolar
DuplicateOutput<9,10> motorpower;//pwm OK. These are the ENA and ENB of the L298, for stepper use there is no reason to just keep one active.

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

#endif

/** input for starting position of cycle. */
InputPin<14> zeroMarker;
///////////////////////////////////////////////////////////////////////////
#include "char.h"

/** adds velocity to position.  */
struct StepperMotor {
  Stepper pos;

  MicroStable ticker;

  /** if and which direction to step*/
  int run = 0;//1,0,-1
  Stepper::Step target = 0;
  bool freeRun = false;

  void setTick(MicroStable::Tick perstep) {
    ticker.set(perstep);
  }

  void operator()() {
    if (ticker.perCycle()) {
      if (!freeRun) {
        run = pos - target;//weird but true, the x= ops are differential, the non x= ops are absolute, and we wish to differential step in a direction determined by absolute position.
      }
      pos += run;
    }
  }

  void stats(ChainPrinter *adbg) {
    if (adbg) {
      (*adbg)("T=", target, " FR=", freeRun, " Step=", int(pos));
    }
  }

  /** stop where it is. */
  void freeze() {
    freeRun = false;
    target = pos;
  }

  /** not actually there until the step has had time to settle down. pessimistically that is when the next step would occur if we were still moving.*/
  bool there() const {
    return run == 0 && target == pos;
  }

  void start(bool second, Stepper::Interface iface) {
    pos.interface = iface;//lambda didn't work
//      [second](byte phase){
//      SpiDualBridgeBoard::setBridge(second,phase);
//    };
    setTick(250000);
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

CLIRP cmd;

//I2C diagnostic
#include "scani2c.h"

void doKey(char key) {
  bool which = key < 'a';//which of two motors
  switch (key) {

  case ' '://report status
    motor[0].stats(&dbg);
    motor[1].stats(&dbg);
    break;

  case 'm'://go to position
    motor[which].target = cmd.arg;
    break;

  case 'Z':
  case 'z': //declare present position is ref
    dbg("marking start");
    motor[which].pos = 0;
    break;

  case 'S':
  case 's'://set stepping rate to use for slewing
    dbg("Setting slew:", cmd.arg);
    motor[which].setTick(cmd.arg);
    break;

  case 'x':
  case 'X': //stop stepping
    dbg("Stopping.");
    motor[which].freeze();
    break;

    //one test system had two relays for switching the motor wiring:
    //  case 'u':
//  case 'U':
//    dbg("unipolar engaged");
//    unipolar = true;
//    break;
//  case 'b':
//  case 'B':
//    dbg("bipolar engaged");
//    unipolar = false;
//    break;

  case 'p':
  case 'P':
    dbg("power on");
    SpiDualBridgeBoard::power(which, true);
    break;

  case 'O':
  case 'o':
    dbg("power off");
    SpiDualBridgeBoard::power(which, false);
    break;

  case 'R':
  case 'r'://free run in reverse
    dbg("Run Reverse.");
    motor[which].freeRun = -1;
    break;

  case 'F':
  case 'f'://run forward
    dbg("Run Forward.");
    motor[which].freeRun = +1;
    break;

  case '?':
    scanI2C(dbg);
#if UsingEDSir
    dbg("IR device is ", IRRX.isPresent() ? "" : "not", " present");
#endif
    break;

  default:
    dbg("Ignored:", char(key), " (", key, ") ", cmd.arg, ',', cmd.pushed);
    return; //don't put in trace buffer
  }
}

void accept(char key) {
  if (cmd.doKey(key)) {
    doKey(key);
  }
}

void doui() {
  while (auto key = dbg.getKey()) { //only checking every milli to save power
    accept(key);
  }
}

/////////////////////////////////////




void setup() {
  Serial.begin(115200);
  dbg("setup");
  SpiDualStepper::start(true);//using true during development, low power until program is ready to run

  motor[0].start(0, bridge0);

  motor[1].start(1, bridge1);

  dbg("setup upsspeed");
}

void loop() {
  if (MicroTicked) {//on avr this is always true, cortexm0, maybe 50%
    motor[0]();
    motor[1]();
  }
  if (MilliTicked) {//no urgent things like debouncing index sensor
    if(zeroMarker){
      motor[0].freeze();
    }
    doui();
  }
}
