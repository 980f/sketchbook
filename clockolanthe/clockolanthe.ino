

#define UsingEDSir 0
#define UsingUDN2540 0
////////////////////////////////////
#include "pinclass.h"
#include "clockserver.h"

#include "cheaptricks.h"

#include "stepper.h"

#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);

//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"
Using_MilliTicker

#if UsingEDSir
#include "edsir.h"
EDSir IRRX;
MonoStable sampleIR(147);//fast enough for a crappy keypad

#endif

const unsigned baseSPR = 2048;//28BYJ-48
const unsigned slewspeed = 5;//5: smooth moving, no load.

class ClockHand {
  public:
    Stepper mechanism;
    //ms per step
    unsigned thespeed = ~0U;
    unsigned stepperrev = baseSPR;
    enum Unit {
      Seconds,
      Minutes,
      Hours,
    };
    Unit unit;

    /** human units to clock angle */
    unsigned timeperrev() const {
      switch (unit) {
        case Seconds: //same as minutes
        case Minutes: return 60;
        case Hours: return 12;
        default: return 0;
      }
    }

    unsigned long msperunit() const {
      switch (unit) {
        case Seconds: return 1000UL;
        case Minutes: return 60 * 1000UL;
        case Hours: return 60 * 60 * 1000UL;
        default: return 0UL;
      }
    }

    //want to be on
    bool enabled = false;
    //think we are on
    bool energized = false; //actually is unknown at init
    //where we want to be
    int target = ~0U;
    //ignore position, run forever in the given direction
    int freerun = 0;

    void freeze() {
      freerun = 0;
      if (changed(enabled, false)) {
        ;
      }
    }

    bool needsPower() {
      return enabled || freerun != 0;
    }

    void setTarget(int target) {
      if (changed(this->target, target)) {
        enabled = target != mechanism;
      }
    }

    void setTime(unsigned hmrs) {
      auto dial = timeperrev(); //size of dial
      setTarget(rate(long(hmrs % dial) * stepperrev , dial)); //protect against stupid input, don't do extra wraps around the face.
    }

    void setFromMillis(unsigned ms) {
      setTime(rate(ms, msperunit()));
    }

    /** declares what the present location is, does not modify target or enable. */
    void setReference(int location) {
      mechanism = location;
    }

    MonoStable ticker;//start up dead
    MonoStable lastStep;//need to wait a bit for power down, maybe also for power up.

    void onTick() {
      if (ticker.perCycle()) {
        if (freerun) {
          mechanism += freerun;
          return;
        }
        if (enabled) {
          mechanism += (target - mechanism);//automatic 'signof' under the hood in Stepper class
          if (target == mechanism) {
            enabled = 0;
            lastStep.set(MilliTick(ticker));
          }
        } else if (lastStep.hasFinished()) {
          mechanism.interface(~0);
        }
      }
    }

    /** update speed, where speed is millis per step */
    void upspeed(unsigned newspeed) {
      if (changed(thespeed, newspeed)) {
        ticker.set(thespeed);//this one will stretch a cycle in progress.
        dbg("\nSpeed:", thespeed);
      }
    }

    //free run with time set so that one revolution happens per real world cycle of hand
    void realtime() {
      upspeed(rate(msperunit()*timeperrev(), stepperrev));
      freerun = 1;
    }

    ClockHand(Unit unit, Stepper::Interface interface): unit(unit) {
      mechanism.interface = interface;
    }

    //placeholder for incomplete code for remote interface
    ClockHand() {

    }
};


/** 4 wire 2 phase unipolar drive */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn> class FourBanger {
  protected:
    OutputPin<xp> mxp;
    OutputPin<xn> mxn;
    OutputPin<yp> myp;
    OutputPin<yn> myn;

  public:
    static bool greylsb(byte step) {
      byte phase = step & 3;
      return (phase == 1) || (phase == 2);
    }

    static bool greymsb(byte step) {
      return (step & 3) >> 1;
    }

    void operator()(byte step) {
      bool x = greylsb(step);
      bool y = greymsb(step);
      mxp = x;
      mxn = !x;
      myp = y;
      myn = !y;
    }

};

/** 4 phase unipolar with power down via nulling all pins. Next step will energize them again. It is a good idea to energize the last settings before changing them, but not absolutely required */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn> class ULN2003: public FourBanger<xp, xn, yp, yn> {
    using Super = FourBanger<xp, xn, yp, yn>;
    //	using FourBanger<xp, xn, yp, yn>;
  public:
    void operator()(byte step) {
      if (step == 255) { //magic value for 'all off'
        powerDown();
      }
      Super::operator()(step);
    }

    void powerDown() {
      //this-> or Super:: needed because C++ isn't yet willing to use the obvious base class.
      this->mxp = 0;
      this->mxn = 0;
      Super::myp = 0;
      this->myn = 0;
    }
};

/** 4 unipolar drive, with common enable. You can PWM the power pin to get lower power, just wiggle it much faster than the load can react. */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn, PinNumberType pwr> class UDN2540: public FourBanger<xp, xn, yp, yn> {
    using Super = FourBanger<xp, xn, yp, yn>;
    OutputPin<pwr> enabler;
  public:
    void operator()(byte step) {
      if (step == 255) { //magic value for 'all off'
        powerDown();
      }
      enabler = 0;
      Super::operator()(step);
    }

    void powerDown() {
      enabler = 1;
    }
};

#if UsingUDN2540
UDN2540<18, 16, 17, 15, 19> minutemotor;
UDN2540<13, 11, 12, 10, 9> hourmotor;
#else
ULN2003<18, 16, 17, 15> minutemotor;
ULN2003<13, 11, 12, 10> hourmotor;
#endif


ClockHand minuteHand(ClockHand::Minutes, [](byte step) {
  minutemotor(step);
});

ClockHand hourHand(ClockHand::Hours, [](byte step) {
  hourmotor(step);
});


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

//StopWatch jerky;//time for jerky motion
MilliTick jerky0 = BadTick;
bool beJerky = false;

//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP cmd;

ClockHand *hand = &minuteHand; //for tweaking one at a time
void reportHand(const ClockHand&hand, const char *which) {
  dbg("\n", which, "T=", hand.target, " en:", hand.enabled, " FR=", hand.freerun, " Step=", hand.mechanism);
}

//I2C diagnostic
#include "scani2c.h"

void doKey(char key) {
  switch (key) {
    case 'T': case 't': //run real time, smoothly
      minuteHand.realtime();
      hourHand.realtime();
      break;

    case 'G': case 'g': //jumpy real time
      upspeedBoth(slewspeed);//move briskly between defined points.
      highnoon();
      //          jerky.start();
      jerky0 = MilliTicked.recent();
      beJerky = true;
      break;

    case ' '://report status
      reportHand(hourHand, "Hour");
      reportHand(minuteHand, "Minute");
      break;

    case 'h'://go to position
      dbg("\nHour stepping to:", cmd.arg);
      hourHand.setTarget(cmd.arg);
      break;

    case ':':
      dbg("\nHour time to:", cmd.arg);
      hourHand.setTime(cmd.arg);
      break;

    case ';':
      dbg("\nMinute time to:", cmd.arg);
      minuteHand.setTime(cmd.arg);
      break;

    case 'm'://go to position
      dbg("\nMinute hand to:", cmd.arg);
      minuteHand.setTarget(cmd.arg);
      break;

    case 'Z'://declare preseent position is noon
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
      beJerky = 0;
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
      break;
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
#if UsingEDSir
/* EDS IR receiver:
     p     c     n
     <     >    ' '
     -     +     =
     0     %     &
     1     2     3
     4     5     6
     7     8     9
*/

char irkey() {
  char irc = IRRX.key(); //I2C read, 0.1 ms\

  switch (irc) {//will map these to char used by keybaord etc.
    case 'p'://run reverse
      return 'R';
    case 'c'://
      return 'x'; //stop tuning
      break;
    case 'n'://run forward
      return 'F';
      break;
    case '<':
      return 'H';
      break;
    case '>':
      return 'M';
      break;
    case ' '://run realtime
      return 't';
      break;
    case '-':
      return 'G';
      break;
    case '+':
      return 'g';
      break;
    case '=':
      return 'Z';
      break;
    case '%'://set hour
      return ':';
      break;
    case '&'://set minute
      return ';';
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return irc;
    case 0: //nothing there
      return 0;
    default:
      dbg("\nUnexpected IR code:", irc);
      return 0;
  }
}
#endif
/////////////////////////////////////

void setup() {
  Serial.begin(115200);
#if UsingEDSir
  IRRX.begin();//our sole i2c user
#endif
  upspeedBoth(slewspeed);
  minuteHand.stepperrev = baseSPR;
  hourHand.stepperrev = baseSPR;
}

void loop() {
  if (MilliTicked) {
    if (beJerky /*jerky.isRunning()*/) { //set time and let target logic move the hand
      MilliTick elapsed = MilliTicked.since(jerky0); //this presumes we set the clock origin to 0 when we record jerky0.
      minuteHand.setFromMillis(elapsed);
      hourHand.setFromMillis(elapsed);
    }
    minuteHand.onTick();
    hourHand.onTick();
    doui();
#if UsingEDSir
    if (sampleIR.perCycle()) {
      accept(irkey());
    }
#endif
  }

}
