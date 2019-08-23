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

#endif

/** input for starting position of cycle. */
InputPin<14> zeroMarker; //14 on leonardo is A0, and so on up.

///////////////////////////////////////////////////////////////////////////
#include "char.h"

/** adds velocity to position.  */
struct StepperMotor {
  Stepper pos;
  Stepper::Step target = 0; //might migrate into Stepper

  /// override run and step at a steady rate forever.
  bool freeRun = false;
  /** if and which direction to step*/
  int run = 0;//1,0,-1

  //time between steps
  MicroStable ticker;

  void setTick(MicroStable::Tick perstep) {
    ticker.set(perstep);
  }

  void operator()() {
    if (ticker.perCycle()) {
      if (!freeRun) {
        run = pos - target;//
      }
      pos += run;//steps if run !0
    }
  }

  void stats(ChainPrinter *adbg) {
    if (adbg) {
      (*adbg)("T=", target, " Step=", int(pos), " FR=", freeRun, " tick:", ticker.duration);
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

    case 'H':
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
  while (auto key = dbg.getKey()) {
    accept(key);
  }
}

/////////////////////////////////////

void setup() {
  Serial.begin(115200);
  dbg("setup");
  SpiDualBridgeBoard::start(true);//using true during development, low power until program is ready to run

  motor[0].start(0, bridge0);

  motor[1].start(1, bridge1);

}

void loop() {
  if (MicroTicked) {//on avr this is always true, cortexm0, maybe 50%
    motor[0]();
    motor[1]();
  }
  if (MilliTicked) {//non urgent things like debouncing index sensor
    if (zeroMarker) {
      motor[0].freeze();
      motor[1].freeze();
    }
    doui();
  }
}
