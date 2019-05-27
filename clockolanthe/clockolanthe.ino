#define UsingLeonardo 0
#define UsingAdalogger 1
#define UsingD1 0
#define UsingIOExpander 0
#define UsingEDSir 1
#define UsingUDN2540 0
////////////////////////////////////
#include "pinclass.h"

#if UsingD1
#include <ESP8266WiFi.h>
#endif

#if UsingIOExpander
#include "pcf8575.h"  //reduced accessed but simplified use
PCF8575 bits; //We need wemos D1 I2C and that leaves us one pin short to direct drive two steppers.
#endif

#include "cheaptricks.h"

#include "stepper.h"

#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);

//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"

#if UsingEDSir
#include "wirewrapper.h"
WireWrapper edsIR(0x60);//todo: see if this off by <<1
WIred<char> irchar(edsIR, 0); //port 0 is asciified map of EDS controller

MonoStable sampleIR(147);//fast enough for a crappy keypad

//declare just to get pullups:
Pin<20, INPUT_PULLUP> SDApup;
Pin<21, INPUT_PULLUP> SCLpup;

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
        } else {
          if (lastStep.hasFinished()) {

          }
        }
      }
    }

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




#if UsingAdalogger
#if UsingUDN2540
UDN2540<18, 16, 17, 15,19> minutemotor;
UDN2540<13, 11, 12, 10,9> hourmotor;
#else
ULN2003<18, 16, 17, 15> minutemotor;
ULN2003<13, 11, 12, 10> hourmotor;
#endif

#elif UsingLeonardo
#if UsingUDN2540
UDN2540<7, 5, 6, 4,14> minutemotor;//todo: separate power pin!
UDN2540<9, 16, 8, 10,14> hourmotor;
#else
ULN2003<7, 5, 6, 4> minutemotor;
ULN2003<9, 16, 8, 10> hourmotor;
#endif

#elif UsingIOExpander
//todo: bits()<=pick 4
ClockHand minuteHand;
ClockHand hourHand;

bool stepperPower;

#else
ClockHand minuteHand;
ClockHand hourHand;

bool stepperPower;
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

/**
  Command Line Interpreter, Reverse Polish input


  If you have a 2-arg function
  then the prior arg is take(pushed)
*/
#include "unsignedrecognizer.h"  //recognize numbers but doesn't deal with +/-


class CLIRP {
    UnsignedRecognizer numberparser;
    //for 2 parameter commands, gets value from param.
  public://until we get template to work.
    unsigned arg = 0;
    unsigned pushed = 0;
  public:
    /** command processor */
    bool doKey(byte key) {
      if (key == 0) { //ignore nulls, might be used for line pacing.
        return false;
      }
      //test digits before ansi so that we can have a numerical parameter for those.
      if (numberparser(key)) { //part of a number, do no more
        return false;
      }
      arg = numberparser; //read and clear, regardless of whether used.
      switch (key) {//used: aAbcdDefFhHiIjlMmNnoprstwxyzZ  :@ *!,.   tab cr newline
        case '\t'://ignore tabs, makes param files easier to read.
          return false;
        case ','://push a parameter for 2 parameter commands.
          pushed = arg;//by not using take() here 1234,X will behave like 1234,1234X
          return false;
      }
      return true;//we did NOT handle it, you look at it.
    }

    template <typename Ret> Ret call(Ret (*fn)(unsigned, unsigned)) {
      (*fn)(take(pushed), arg);
    }

    template <typename Ret> Ret call(Ret (*fn)(unsigned)) {
      pushed = 0; //forget unused arg.
      (*fn)(arg);
    }

};

CLIRP cmd;
ClockHand *hand = &minuteHand; //for tweaking one at a time

void reportHand(const ClockHand&hand, const char *which) {
  dbg("\n", which, "T=", hand.target, " en:", hand.enabled, " FR=", hand.freerun, " Step=", hand.mechanism);
}


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
      dbg("\nIR device is ", edsIR.isPresent() ? "" : "not", " present");
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
  char irc = irchar.fetch(); //I2C read, 0.1 ms\

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
  edsIR.begin();//our sole i2c user
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
