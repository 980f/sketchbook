
#include "options.h"


////////////////////////////////////
#include "pinclass.h"
#include "cheaptricks.h"
#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);


//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"
Using_MilliTicker


#include "stepper.h"
#include "motordrivers.h"
#include "clockhand.h"

//project specific values:
const unsigned baseSPR = 2048;//28BYJ-48
const unsigned slewspeed = 5;//5: smooth moving, no load.



#if UsingDRV8833

#ifdef ADAFRUIT_FEATHER_M0
DRV8833<10, 9, 12, 11, 13> minutemotor;
DRV8833< 16, 15, 17, 18, 14> hourmotor;
//void hourmotor(byte step){}
#else  //presume ProMicro/Leonardo
DRV8833<12, 11, 10, 9, 13> minutemotor;
DRV8833<16, 17, 5, 18, 19> hourmotor;

#endif
#else
//#error "must define one of the driver classes such as UsingUDN2540, UsingDRV8833 ..."
void minutemotor(byte step) {}
void hourmotor(byte step) {}
#endif

///////////////////////////////////////////////////////////////////////////
#include "char.h"
char steptrace[32 + 1];
unsigned stepcount = 0;

ClockHand minuteHand(ClockHand::Minutes, [](byte step) {
  minutemotor(step);
  steptrace[stepcount++] = Char(motorNibble(step)).hexNibble(0);
  stepcount %= 32;
});

ClockHand hourHand(ClockHand::Hours, [](byte step) {
  hourmotor(step);
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
      //      if (myHand) {
      if (fwd) {
        myHand.freerun = +1;
        myHand.upspeed ( slewspeed);
      } else if (rev) {
        myHand.freerun = -1;
        myHand.upspeed ( slewspeed);
      } else {
        myHand.realtime();
      }
      //      }
    }
};

SlewControl<20, 21> minuteSlew(minuteHand);
SlewControl<23, 24> hourSlew(hourHand);

//arduino has issues with a plain function name matching that of a class in the same file!
void upspeedBoth(unsigned msperstep) {
  minuteHand.upspeed(msperstep);
  hourHand.upspeed(msperstep);
}

void freezeBoth() {
  minuteHand.freeze();
  hourHand.freeze();
}

//now is reference position. Might be 5:00 a.m. for act II of Iolanthe
void highnoon() {
  minuteHand.setReference(0);
  hourHand.setReference(0);
}



#include "clockdriver.h"

//interface to local logic
ClockDriver::ClockDriver():
  target(0)
{
  //#done.
}

void ClockDriver::runReal() {
  minuteHand.realtime();
  hourHand.realtime();
}

void ClockDriver::setMinute(unsigned arg, bool raw) {
  if (raw) {
    dbg("\nMinute step to:", arg);
    minuteHand.setTarget(arg);
  } else {
    dbg("\nMinute time to:", arg);
    minuteHand.setTime(target.minute = arg);
  }
}

void ClockDriver::setHour(unsigned arg, bool raw) {
  if (raw) {
    dbg("\nHour stepping to:", arg);
    hourHand.setTarget(arg);
  } else {
    dbg("\nHour time to:", arg);
    hourHand.setTime(target.hour = arg);
  }
}

void reportHand(const ClockHand&hand, const char *which) {
  dbg("\n", which, "T=", hand.target, " en:", hand.enabled, " FR=", hand.freerun, " Step=", hand.mechanism);
}


void ClockDriver::dump() {
  reportHand(hourHand, "Hour");
  reportHand(minuteHand, "Minute");
}

ClockDriver bigben;

//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP cmd;

ClockHand *hand = &minuteHand; //for tweaking one at a time

//I2C diagnostic
#include "scani2c.h"


void doKey(char key) {
  switch (key) {
    case 'T': case 't': //run real time, smoothly
      bigben.runReal();
      break;

    case ' '://report status
      bigben.dump();

      dbg("\nTrace:", steptrace);
      break;

    case 'h'://go to position
      bigben.setHour(cmd.arg, true);
      break;

    case 'm'://go to position
      bigben.setMinute(cmd.arg, true);
      break;

    case ':':
      bigben.setHour(cmd.arg);
      break;

    case ';':
      bigben.setMinute(cmd.arg);
      break;

    case 'Z'://declare present position is noon
      dbg("\n marking noon");
      highnoon();
      break;

    case 'v'://set stepping rate to use
      dbg("\nSetting step:", cmd.arg);
      upspeedBoth(cmd.arg);
      break;

    case 'x'://stop stepping of both
      dbg("\nStopping.");
      freezeBoth();
      break;

    case 'H':
      dbg("\nfocussing on Hour hand");
      hand = &hourHand;
      break;

    case 'M':
      dbg("\nfocussing on minute hand");
      hand = &minuteHand;
      break;

    case 'R'://free run in reverse
      dbg("\nRun Reverse.");
      hand->freerun = -1;
      break;

    case 'F'://run forward
      dbg("\nRun Forward.");
      hand->freerun = +1;
      break;

    case 'X'://stop just the one
      hand->freeze();
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
  upspeedBoth(slewspeed);
  dbg("setup basespr");
  minuteHand.stepperrev = baseSPR;
  minutemotor.enabler = 1;

  hourHand.stepperrev = baseSPR; //todo: pulley ratio goes here
  hourmotor.enabler = 1;

  //here is where we would configure step duration.
  bigben.runReal(); //power cycle at least gets us moving.
}


void loop() {
  if (MilliTicked) {
    minuteSlew.onTick();
    hourSlew.onTick();
    //if both need to run, minute gets priority (in case we share control lines)
    if (!minuteHand.onTick()) {
      hourHand.onTick();
    }
    doui();
  }
}
