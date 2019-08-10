
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
//#include "millievent.h"
//Using_MilliTicker

#include "microevent.h"
Using_MicroTicker

#include "stepper.h"
#include "motordrivers.h"

//project specific values:
const unsigned baseSPR = 2048;//28BYJ-48

//todo: code in seconds so that we can switch between micro and milli
unsigned slewspeed = 50;//5: 28BJY48 smooth moving, no load.



#ifdef ADAFRUIT_FEATHER_M0
FourBanger<10, 9, 12, 11> minutemotor;
OutputPin<17> unipolar;//else bipolar
OutputPin<18> motorpower;//relay, don't pwm this!
#else  //presume ProMicro/Leonardo
FourBanger<12, 11, 10, 9> minutemotor;
bool unipolar;
bool motorpower;//todo: assign real pins
#endif


///////////////////////////////////////////////////////////////////////////
#include "char.h"
char steptrace[32 + 1];//debug
unsigned stepcount = 0;

#include "clockhand.h"

ClockHand minuteHand(ClockHand::Minutes, [](byte step) {
  minutemotor(step);
  steptrace[stepcount++] = Char(motorNibble(step)).hexNibble(0);
  stepcount %= 32;//diagnostic loop, unrelated to stepper action. Used to look for erratic timebase.
});



template <PinNumberType forward, PinNumberType reverse> class SlewControl {
  public: //for diagnostics
    InputPin<forward> fwd;
    InputPin<reverse> rev;
  public: //easier to set after construction.
    ClockHand &myHand;
    SlewControl(ClockHand & myHand): myHand(myHand) {
      //#set myHand on setup()
    }

    void onTick() { //every millisecond
      if (fwd) {
        myHand.freerun = +1;
        myHand.upspeed ( slewspeed);
      } else if (rev) {
        myHand.freerun = -1;
        myHand.upspeed ( slewspeed);
      } else {
        myHand.realtime();
      }
    }
};

//if adalogger:
SlewControl<23, 20> minuteSlew(minuteHand);


//now is reference position. Might be 5:00 a.m. for act II of Iolanthe
void highnoon() {
  minuteHand.setReference(0);
}


void reportHand(const ClockHand&hand, const char *which) {
  dbg("\n", which, "T=", hand.target, " en:", hand.enabled, " FR=", hand.freerun, " Step=", hand.mechanism);
}

//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP cmd;

//I2C diagnostic
#include "scani2c.h"


void doKey(char key) {
  switch (key) {
    case 'T': case 't': //run real time, smoothly
      minuteHand.realtime();
      break;

    case ' '://report status
      reportHand(minuteHand, "Minute");
      dbg("\nTrace:", steptrace);
      break;

    case 'm'://go to position
      minuteHand.setTarget(cmd.arg);
      break;

    case ';':
      minuteHand.setTime(cmd.arg);
      break;

    case 'Z'://declare present position is noon
      dbg("\n marking noon");
      highnoon();
      break;

    case 'v'://set stepping rate to use for timekeeping
      dbg("\nSetting step:", cmd.arg);
      minuteHand.upspeed(cmd.arg);
      break;

    case 's'://set stepping rate to use for slewing
      dbg("\nSetting slew:", cmd.arg);
      slewspeed = cmd.arg;
      break;


    case 'x': case 'X': //stop stepping
      dbg("\nStopping.");
      minuteHand.freeze();
      break;

    case 'u': case 'U':
      dbg("\nunipolar engaged");
      unipolar = 1;
      break;
    case 'b': case 'B':
      dbg("\nbipolar engaged");
      unipolar = 0;
      break;

    case 'p': case 'P':
      dbg("\npower on");
      motorpower = 1;
      break;
    case 'o': case 'O':
      dbg("\npower off");
      motorpower = 0;
      break;


    case 'R'://free run in reverse
      dbg("\nRun Reverse.");
      minuteHand.freerun = -1;
      break;

    case 'F'://run forward
      dbg("\nRun Forward.");
      minuteHand.freerun = +1;
      break;


    case '?':
      scanI2C(dbg);
#if UsingEDSir
      dbg("\nIR device is ", IRRX.isPresent() ? "" : "not", " present");
#endif
      break;

    default:
      dbg("\nIgnored:", char(key), " (", key, ") ", cmd.arg, ',', cmd.pushed);
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
  steptrace[32] = 0; //terminate debug text
  Serial.begin(115200);
  dbg("setup");

  dbg("setup upsspeed");
  minuteHand.upspeed(slewspeed);
  dbg("setup basespr");
  minuteHand.stepperrev = baseSPR;
  motorpower = 1;


  //here is where we would configure step duration.
  minuteHand.realtime(); //power cycle at least gets us moving.
}

MicroTick::RawTick ticks[500 + 1];
unsigned microTickTracer = 0;

void loop() {
  if (MicroTicked) {
    if (true) {
      if (microTickTracer < 500) {
        ticks[microTickTracer++] = MicroTicked.recent().micros; //~10 ms per loop
      } else {
        while (microTickTracer > 0) {
          dbg(ticks[--microTickTracer]);
        }
      }
    }
    minuteSlew.onTick();
    //if both need to run, minute gets priority (in case we share control lines)
    if (!minuteHand.onTick()) {
      //      hourHand.onTick();
    }
    doui();
  }
}
