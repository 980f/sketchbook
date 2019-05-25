#define UsingLeonardo 1
#define UsingD1 0
#define UsingIOExpander 0
////////////////////////////////////
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

    bool enabled = false;
    int target = ~0U;
    int freerun = 0;

    void freeze() {
      freerun = 0;
      enabled = false;
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
      setTarget(rate(long(hmrs) * stepperrev , timeperrev()));
    }

    void setFromMillis(unsigned ms) {
      setTime(rate(ms, msperunit()));
    }

    /** declares what the present location is, does not modify target or enable. */
    void setReference(int location) {
      mechanism = location;
    }

    MonoStable ticker;//start up dead

    void onTick() {
      if (ticker.perCycle()) {
        if (freerun) {
          mechanism += freerun;
        } else if (enabled) {
          mechanism += (target - mechanism);//automatic 'signof' under the hood in Stepper class
          if (target == mechanism) {
            enabled = 0;
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

    //free run with time set so that one revolution happens
    void realtime() {
      upspeed(rate(msperunit()*timeperrev(), stepperrev)); //milliseconds /
      freerun = 1;
    }

    ClockHand(Unit unit, Stepper::Interface interface): unit(unit) {
      mechanism.interface = interface;
    }

    ClockHand() {

    }
};

#if UsingLeonardo
#include "pinclass.h"
//D4..D7  order chosen for wiring convenience
OutputPin<7> mxp;
OutputPin<5> mxn;
OutputPin<6> myp;
OutputPin<4> myn;


bool greylsb(byte step) {
  byte phase = step & 3;
  return (phase == 1) || (phase == 2);
}

bool greymsb(byte step) {
  return (step & 3) >> 1;
}

ClockHand minuteHand(ClockHand::Minutes, [](byte step) {
  bool x = greylsb(step);
  bool y = greymsb(step);
  mxp = x;
  mxn = !x;
  myp = y;
  myn = !y;
});

OutputPin<9> hxp;
OutputPin<16> hxn;
OutputPin<8> hyp;
OutputPin<10> hyn;

ClockHand hourHand(ClockHand::Hours, [](byte step) {
  bool x = greylsb(step);
  bool y = greymsb(step);
  hxp = x;
  hxn = !x;
  hyp = y;
  hyn = !y;
});

//UDN2540 driver supports this natively, will be ignored by uln2003 boards. A true pwm pin would be a good choice.
OutputPin<14> stepperPower;

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

void doui() {
  while (auto key = dbg.getKey()) { //only checking every milli to save power
    if (cmd.doKey(key)) {
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

        default:
          dbg("\nIgnored:", char(key), " (", key, ") ", cmd.arg, ',', cmd.pushed);
          break;
      }
    }//end command
  }
}

/////////////////////////////////////

void setup() {
  Serial.begin(115200);
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
    stepperPower = hourHand.needsPower() || minuteHand.needsPower();
  }
}
